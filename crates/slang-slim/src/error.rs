use std::{error::Error as StdError, fmt, ptr, slice};

use slang_slim_sys as sys;

/// Diagnostic text or bytes emitted by Slang.
///
/// Slang commonly returns UTF-8 diagnostics, but the native API exposes an
/// arbitrary blob. Keeping the bytes preserves the original data while
/// [`Diagnostics::to_string_lossy`] provides a convenient display form.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct Diagnostics(Vec<u8>);

impl Diagnostics {
    pub(crate) fn from_bytes(bytes: Vec<u8>) -> Self {
        Self(bytes)
    }

    /// Returns the diagnostic bytes exactly as returned by Slang.
    pub fn as_bytes(&self) -> &[u8] {
        &self.0
    }

    /// Consumes this value and returns the diagnostic bytes.
    pub fn into_bytes(self) -> Vec<u8> {
        self.0
    }

    /// Returns whether Slang returned no diagnostic bytes.
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }

    /// Decodes diagnostics as UTF-8, replacing malformed sequences.
    pub fn to_string_lossy(&self) -> String {
        String::from_utf8_lossy(&self.0).into_owned()
    }
}

impl AsRef<[u8]> for Diagnostics {
    fn as_ref(&self) -> &[u8] {
        self.as_bytes()
    }
}

impl fmt::Display for Diagnostics {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.to_string_lossy())
    }
}

/// A failed Slang operation and any diagnostics it produced.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Error {
    status: sys::SlangResult,
    diagnostics: Diagnostics,
}

impl Error {
    pub(crate) fn from_status(status: sys::SlangResult) -> Self {
        Self {
            status,
            diagnostics: Diagnostics::default(),
        }
    }

    pub(crate) fn from_status_and_diagnostics(
        status: sys::SlangResult,
        diagnostics: Diagnostics,
    ) -> Self {
        Self {
            status,
            diagnostics,
        }
    }

    pub(crate) fn invalid_argument() -> Self {
        Self::from_status(sys::SLANG_E_INVALID_ARG)
    }

    pub(crate) fn out_of_memory() -> Self {
        Self::from_status(sys::SLANG_E_OUT_OF_MEMORY)
    }

    /// Returns the original SlangResult value.
    pub fn status(&self) -> sys::SlangResult {
        self.status
    }

    /// Returns diagnostics associated with this failed operation.
    pub fn diagnostics(&self) -> &Diagnostics {
        &self.diagnostics
    }

    /// Consumes this error and returns its diagnostics.
    pub fn into_diagnostics(self) -> Diagnostics {
        self.diagnostics
    }
}

impl fmt::Display for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.diagnostics.is_empty() {
            write!(
                formatter,
                "Slang operation failed with status {}",
                self.status
            )
        } else {
            write!(
                formatter,
                "Slang operation failed with status {}: {}",
                self.status, self.diagnostics
            )
        }
    }
}

impl StdError for Error {}

/// The value produced by a successful operation and optional warning or
/// informational diagnostics. A non-negative SlangResult is success, so
/// diagnostics may be present even when the operation succeeds.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Output<T> {
    /// The operation's primary result.
    pub value: T,
    /// The original non-negative SlangResult value.
    pub status: sys::SlangResult,
    /// Warning or informational diagnostics emitted by Slang.
    pub diagnostics: Diagnostics,
}

impl<T> Output<T> {
    pub(crate) fn new(status: sys::SlangResult, value: T, diagnostics: Diagnostics) -> Self {
        Self {
            value,
            status,
            diagnostics,
        }
    }

    /// Borrows the primary result.
    pub fn value(&self) -> &T {
        &self.value
    }

    /// Consumes the output and returns the primary result.
    pub fn into_value(self) -> T {
        self.value
    }

    /// Returns the original non-negative SlangResult value.
    pub fn status(&self) -> sys::SlangResult {
        self.status
    }

    /// Returns whether warning or informational diagnostics are present.
    pub fn has_diagnostics(&self) -> bool {
        !self.diagnostics.is_empty()
    }
}

impl<T> AsRef<T> for Output<T> {
    fn as_ref(&self) -> &T {
        self.value()
    }
}

/// Result type used by safe wrapper operations.
pub type Result<T> = std::result::Result<T, Error>;

pub(crate) fn is_success(status: sys::SlangResult) -> bool {
    status >= 0
}

/// Copies a native blob and releases its native reference.
///
/// The C ABI intentionally exposes only a release operation for blobs. The
/// safe layer therefore copies the data before releasing the handle, making
/// returned code and diagnostics independent Rust-owned byte vectors.
pub(crate) unsafe fn take_blob(blob: *mut sys::ISlangBlob) -> Vec<u8> {
    if blob.is_null() {
        return Vec::new();
    }

    let size = unsafe { sys::slang_blob_get_buffer_size(blob) };
    let data = unsafe { sys::slang_blob_get_buffer_pointer(blob) };
    let bytes = if size == 0 || data.is_null() {
        Vec::new()
    } else {
        unsafe { slice::from_raw_parts(data.cast::<u8>(), size) }.to_vec()
    };
    unsafe { sys::slang_blob_destroy(blob) };
    bytes
}

pub(crate) struct RawBlob {
    raw: ptr::NonNull<sys::ISlangBlob>,
}

impl RawBlob {
    pub(crate) fn new(data: &[u8]) -> Result<Self> {
        let mut raw = ptr::null_mut();
        let data_ptr = if data.is_empty() {
            ptr::null()
        } else {
            data.as_ptr().cast()
        };
        let status = unsafe { sys::slang_create_blob(data_ptr, data.len(), &mut raw) };
        if !is_success(status) {
            return Err(Error::from_status(status));
        }
        let Some(raw) = ptr::NonNull::new(raw) else {
            return Err(Error::out_of_memory());
        };
        Ok(Self { raw })
    }

    pub(crate) fn as_ptr(&self) -> *mut sys::ISlangBlob {
        self.raw.as_ptr()
    }
}

impl Drop for RawBlob {
    fn drop(&mut self) {
        unsafe { sys::slang_blob_destroy(self.raw.as_ptr()) };
    }
}

pub(crate) fn diagnostics_from_raw(blob: *mut sys::ISlangBlob) -> Diagnostics {
    Diagnostics::from_bytes(unsafe { take_blob(blob) })
}

pub(crate) fn finish<T>(
    status: sys::SlangResult,
    value: T,
    diagnostics_blob: *mut sys::ISlangBlob,
) -> Result<Output<T>> {
    let diagnostics = diagnostics_from_raw(diagnostics_blob);
    if is_success(status) {
        Ok(Output::new(status, value, diagnostics))
    } else {
        Err(Error::from_status_and_diagnostics(status, diagnostics))
    }
}
