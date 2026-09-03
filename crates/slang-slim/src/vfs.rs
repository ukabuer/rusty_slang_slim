use std::{
    any::Any,
    ffi::{CStr, c_char, c_void},
    panic::{AssertUnwindSafe, catch_unwind},
    ptr,
    rc::Rc,
    sync::Arc,
};

use slang_slim_sys as sys;

use crate::{Error, Result, error::RawBlob};

/// A synchronous virtual file system used by Slang while loading modules.
///
/// `path` is the UTF-8 path requested by Slang. Returning an error passes the
/// supplied SlangResult back through the native callback. Implementations
/// must not panic across the C ABI; a panic is caught and reported as
/// `SLANG_FAIL` by the adapter.
pub trait VirtualFileSystem: Send + Sync + 'static {
    fn load_file(&self, path: &str) -> std::result::Result<Vec<u8>, sys::SlangResult>;
}

impl<F> VirtualFileSystem for F
where
    F: Fn(&str) -> std::result::Result<Vec<u8>, sys::SlangResult> + Send + Sync + 'static,
{
    fn load_file(&self, path: &str) -> std::result::Result<Vec<u8>, sys::SlangResult> {
        self(path)
    }
}

/// A native Slang file-system adapter backed by a Rust [`VirtualFileSystem`].
///
/// Cloning this value shares one native adapter and one callback state. The
/// callback state is retained by sessions created with this file system, so
/// the Rust implementation may be dropped by the caller immediately after
/// session creation, just like Slang's own COM-style file-system objects.
#[derive(Clone)]
pub struct FileSystem {
    pub(crate) inner: Rc<FileSystemInner>,
}

pub(crate) struct FileSystemInner {
    raw: ptr::NonNull<sys::ISlangFileSystem>,
    // This trait object deliberately owns the callback state independently of
    // the native handle. Slang does not know how to retain Rust user data.
    pub(crate) state: Arc<dyn VfsKeepAlive>,
}

/// Private type-erased owner used to keep generic callback state alive.
pub(crate) trait VfsKeepAlive: Any + Send + Sync {}
impl<T: Any + Send + Sync> VfsKeepAlive for T {}

struct CallbackState<F> {
    filesystem: F,
}

impl FileSystem {
    /// Creates a Slang file-system adapter for a Rust implementation.
    pub fn new<F>(filesystem: F) -> Result<Self>
    where
        F: VirtualFileSystem,
    {
        let state = Arc::new(CallbackState { filesystem });
        let user_data = Arc::as_ptr(&state).cast_mut().cast::<c_void>();
        let desc = sys::SlangFileSystemDesc {
            structure_size: std::mem::size_of::<sys::SlangFileSystemDesc>(),
            load_file: Some(load_file_callback::<F>),
            load_file_user_data: user_data,
        };

        let mut raw = ptr::null_mut();
        let status = unsafe { sys::slang_file_system_create(&desc, &mut raw) };
        if status < 0 {
            return Err(Error::from_status(status));
        }
        let Some(raw) = ptr::NonNull::new(raw) else {
            return Err(Error::from_status(sys::SLANG_FAIL));
        };

        let state: Arc<dyn VfsKeepAlive> = state;
        Ok(Self {
            inner: Rc::new(FileSystemInner { raw, state }),
        })
    }

    pub(crate) fn raw(&self) -> *mut sys::ISlangFileSystem {
        self.inner.raw.as_ptr()
    }
}

impl Drop for FileSystemInner {
    fn drop(&mut self) {
        // Keep `state` alive until after the native adapter has been released:
        // a release may synchronously finish work that can invoke the
        // callback.
        unsafe { sys::slang_file_system_destroy(self.raw.as_ptr()) };
    }
}

unsafe extern "C" fn load_file_callback<F>(
    user_data: *mut c_void,
    path: *const c_char,
    out_blob: *mut *mut sys::ISlangBlob,
) -> sys::SlangResult
where
    F: VirtualFileSystem,
{
    if user_data.is_null() || path.is_null() || out_blob.is_null() {
        return sys::SLANG_E_INVALID_ARG;
    }

    unsafe {
        *out_blob = ptr::null_mut();
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let path = unsafe { CStr::from_ptr(path) };
        let path = match path.to_str() {
            Ok(path) => path,
            Err(_) => return Err(sys::SLANG_E_INVALID_ARG),
        };
        let state = unsafe { &*(user_data.cast::<CallbackState<F>>()) };
        let bytes = state.filesystem.load_file(path)?;
        let blob = RawBlob::new(&bytes).map_err(|error| error.status())?;
        let raw = blob.as_ptr();
        // Transfer the one native reference created by RawBlob to Slang.
        std::mem::forget(blob);
        unsafe {
            *out_blob = raw;
        }
        Ok(sys::SLANG_OK)
    }));

    match result {
        Ok(Ok(status)) => status,
        Ok(Err(status)) => status,
        Err(_) => sys::SLANG_FAIL,
    }
}

#[cfg(all(test, feature = "native"))]
mod tests {
    use super::*;
    use std::ffi::CString;

    type Loader = fn(&str) -> std::result::Result<Vec<u8>, sys::SlangResult>;

    fn invoke(loader: Loader, path: &CString) -> (sys::SlangResult, *mut sys::ISlangBlob) {
        let state = CallbackState { filesystem: loader };
        let user_data = (&state as *const CallbackState<Loader>)
            .cast_mut()
            .cast::<c_void>();
        let mut blob = ptr::null_mut();
        let status = unsafe { load_file_callback::<Loader>(user_data, path.as_ptr(), &mut blob) };
        (status, blob)
    }

    #[test]
    fn callback_accepts_empty_file() {
        let path = CString::new("empty.hlsl").unwrap();
        let (status, blob) = invoke(|_| Ok(Vec::new()), &path);
        assert_eq!(status, sys::SLANG_OK);
        assert!(!blob.is_null());
        assert_eq!(unsafe { sys::slang_blob_get_buffer_size(blob) }, 0);
        unsafe { sys::slang_blob_destroy(blob) };
    }

    #[test]
    fn callback_preserves_loader_error() {
        let path = CString::new("missing.hlsl").unwrap();
        let (status, blob) = invoke(|_| Err(sys::SLANG_E_CANNOT_OPEN), &path);
        assert_eq!(status, sys::SLANG_E_CANNOT_OPEN);
        assert!(blob.is_null());
    }

    #[test]
    fn callback_converts_panic_to_failure() {
        let path = CString::new("panic.hlsl").unwrap();
        let (status, blob) = invoke(
            |_| -> std::result::Result<Vec<u8>, sys::SlangResult> {
                panic!("intentional VFS callback panic")
            },
            &path,
        );
        assert_eq!(status, sys::SLANG_FAIL);
        assert!(blob.is_null());
    }
}
