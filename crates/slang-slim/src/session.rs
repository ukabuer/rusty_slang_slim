use std::{
    ffi::{CStr, CString},
    ptr,
    rc::Rc,
    sync::Arc,
};

use slang_slim_sys as sys;

use crate::{
    Error, Output, Result,
    component::{ComponentKind, ComponentType},
    error::{RawBlob, finish, is_success},
    vfs::{FileSystem, VfsKeepAlive},
};

/// Options passed when creating a Slang global session.
#[derive(Clone, Copy, Debug)]
pub struct GlobalSessionDesc {
    /// Slang API version requested by the caller.
    pub api_version: u32,
    /// Minimum Slang language version accepted by the session.
    pub min_language_version: u32,
    /// Whether GLSL support should be enabled in the global session.
    pub enable_glsl: bool,
}

impl Default for GlobalSessionDesc {
    fn default() -> Self {
        Self {
            api_version: sys::SLANG_API_VERSION,
            min_language_version: sys::SLANG_LANGUAGE_VERSION_2025,
            enable_glsl: false,
        }
    }
}

impl GlobalSessionDesc {
    fn to_raw(self) -> sys::SlangGlobalSessionDesc {
        sys::SlangGlobalSessionDesc {
            structure_size: std::mem::size_of::<sys::SlangGlobalSessionDesc>() as u32,
            api_version: self.api_version,
            min_language_version: self.min_language_version,
            enable_glsl: self.enable_glsl as u8,
            _enable_glsl_padding: [0; 3],
            reserved: [0; 16],
        }
    }
}

/// A compiler option value used by a target or session descriptor.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum CompilerOption {
    /// An integer option. `value1` is available for options that use two
    /// integer fields.
    Int {
        name: sys::CompilerOptionName,
        value0: i32,
        value1: i32,
    },
    /// A string option with an optional second string field.
    String {
        name: sys::CompilerOptionName,
        value0: String,
        value1: Option<String>,
    },
}

impl CompilerOption {
    /// Creates a single-value integer compiler option.
    pub fn int(name: sys::CompilerOptionName, value: i32) -> Self {
        Self::Int {
            name,
            value0: value,
            value1: 0,
        }
    }

    /// Creates an integer compiler option with two values.
    pub fn int_pair(name: sys::CompilerOptionName, value0: i32, value1: i32) -> Self {
        Self::Int {
            name,
            value0,
            value1,
        }
    }

    /// Creates a string compiler option.
    pub fn string(name: sys::CompilerOptionName, value: impl Into<String>) -> Self {
        Self::String {
            name,
            value0: value.into(),
            value1: None,
        }
    }

    /// Creates a string compiler option with two string values.
    pub fn string_pair(
        name: sys::CompilerOptionName,
        value0: impl Into<String>,
        value1: impl Into<String>,
    ) -> Self {
        Self::String {
            name,
            value0: value0.into(),
            value1: Some(value1.into()),
        }
    }
}

struct RawCompilerOptions {
    entries: Vec<sys::CompilerOptionEntry>,
    // CString owns the allocations referenced by entries. Moving a CString
    // does not move its NUL-terminated allocation, so the pointers remain
    // valid while this storage is alive.
    strings: Vec<CString>,
}

impl RawCompilerOptions {
    fn new(options: &[CompilerOption]) -> Result<Self> {
        let mut entries = Vec::with_capacity(options.len());
        let mut strings = Vec::new();

        for option in options {
            let entry = match option {
                CompilerOption::Int {
                    name,
                    value0,
                    value1,
                } => sys::CompilerOptionEntry {
                    name: *name,
                    value: sys::CompilerOptionValue {
                        kind: sys::COMPILER_OPTION_VALUE_INT,
                        int_value0: *value0,
                        int_value1: *value1,
                        string_value0: ptr::null(),
                        string_value1: ptr::null(),
                    },
                },
                CompilerOption::String {
                    name,
                    value0,
                    value1,
                } => {
                    let value0 = make_cstring(value0)?;
                    let value0_ptr = value0.as_ptr();
                    strings.push(value0);

                    let value1_ptr = if let Some(value1) = value1 {
                        let value1 = make_cstring(value1)?;
                        let value1_ptr = value1.as_ptr();
                        strings.push(value1);
                        value1_ptr
                    } else {
                        ptr::null()
                    };

                    sys::CompilerOptionEntry {
                        name: *name,
                        value: sys::CompilerOptionValue {
                            kind: sys::COMPILER_OPTION_VALUE_STRING,
                            int_value0: 0,
                            int_value1: 0,
                            string_value0: value0_ptr,
                            string_value1: value1_ptr,
                        },
                    }
                }
            };
            entries.push(entry);
        }

        Ok(Self { entries, strings })
    }

    fn as_ptr(&self) -> *const sys::CompilerOptionEntry {
        if self.entries.is_empty() {
            ptr::null()
        } else {
            self.entries.as_ptr()
        }
    }

    fn len(&self) -> u32 {
        self.entries.len() as u32
    }

    fn keep_strings_alive(&self) {
        // This method makes the ownership relationship explicit at call
        // sites. The vector is otherwise intentionally only read by Slang.
        let _ = self.strings.len();
    }
}

/// A code-generation target in a [`SessionDesc`].
#[derive(Clone, Debug)]
pub struct TargetDesc {
    pub format: sys::SlangCompileTarget,
    pub profile: sys::SlangProfileID,
    pub flags: sys::SlangTargetFlags,
    pub floating_point_mode: sys::SlangFloatingPointMode,
    pub line_directive_mode: sys::SlangLineDirectiveMode,
    pub force_glsl_scalar_buffer_layout: bool,
    pub compiler_options: Vec<CompilerOption>,
}

impl TargetDesc {
    /// Creates a target with Slang's default target flags and modes.
    pub fn new(format: sys::SlangCompileTarget, profile: sys::SlangProfileID) -> Self {
        Self {
            format,
            profile,
            flags: sys::K_DEFAULT_TARGET_FLAGS,
            floating_point_mode: sys::SLANG_FLOATING_POINT_MODE_DEFAULT,
            line_directive_mode: sys::SLANG_LINE_DIRECTIVE_MODE_DEFAULT,
            force_glsl_scalar_buffer_layout: false,
            compiler_options: Vec::new(),
        }
    }

    /// Appends a target-specific compiler option.
    pub fn add_compiler_option(&mut self, option: CompilerOption) {
        self.compiler_options.push(option);
    }

    fn to_raw(&self, options: &RawCompilerOptions) -> sys::SlangTargetDesc {
        options.keep_strings_alive();
        sys::SlangTargetDesc {
            structure_size: std::mem::size_of::<sys::SlangTargetDesc>(),
            format: self.format,
            profile: self.profile,
            flags: self.flags,
            floating_point_mode: self.floating_point_mode,
            line_directive_mode: self.line_directive_mode,
            force_glsl_scalar_buffer_layout: self.force_glsl_scalar_buffer_layout as u8,
            _force_glsl_scalar_buffer_layout_padding: [0; 3],
            compiler_option_entries: options.as_ptr(),
            compiler_option_entry_count: options.len(),
        }
    }
}

/// Options passed when creating a Slang session.
pub struct SessionDesc<'a> {
    pub targets: Vec<TargetDesc>,
    pub flags: sys::SessionFlags,
    pub default_matrix_layout_mode: sys::SlangMatrixLayoutMode,
    pub search_paths: Vec<String>,
    pub preprocessor_macros: Vec<(String, String)>,
    pub file_system: Option<&'a FileSystem>,
    pub enable_effect_annotations: bool,
    pub allow_glsl_syntax: bool,
    pub compiler_options: Vec<CompilerOption>,
    pub skip_spirv_validation: bool,
}

impl<'a> Default for SessionDesc<'a> {
    fn default() -> Self {
        Self {
            targets: Vec::new(),
            flags: sys::K_SESSION_FLAGS_NONE,
            default_matrix_layout_mode: sys::SLANG_MATRIX_LAYOUT_ROW_MAJOR,
            search_paths: Vec::new(),
            preprocessor_macros: Vec::new(),
            file_system: None,
            enable_effect_annotations: false,
            allow_glsl_syntax: false,
            compiler_options: Vec::new(),
            skip_spirv_validation: false,
        }
    }
}

impl<'a> SessionDesc<'a> {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn add_target(&mut self, target: TargetDesc) {
        self.targets.push(target);
    }

    pub fn add_search_path(&mut self, path: impl Into<String>) {
        self.search_paths.push(path.into());
    }

    pub fn add_preprocessor_macro(&mut self, name: impl Into<String>, value: impl Into<String>) {
        self.preprocessor_macros.push((name.into(), value.into()));
    }

    /// Appends a session-wide compiler option.
    pub fn add_compiler_option(&mut self, option: CompilerOption) {
        self.compiler_options.push(option);
    }

    pub fn set_file_system(&mut self, file_system: &'a FileSystem) {
        self.file_system = Some(file_system);
    }
}

pub(crate) struct GlobalSessionInner {
    pub(crate) raw: ptr::NonNull<sys::IGlobalSession>,
}

/// A Slang global session.
#[derive(Clone)]
pub struct GlobalSession {
    pub(crate) inner: Rc<GlobalSessionInner>,
}

impl GlobalSession {
    /// Creates a global session with [`GlobalSessionDesc::default`].
    pub fn new() -> Result<Self> {
        Self::with_desc(GlobalSessionDesc::default())
    }

    /// Creates a global session with explicit Slang global options.
    pub fn with_desc(desc: GlobalSessionDesc) -> Result<Self> {
        let raw_desc = desc.to_raw();
        let mut raw = ptr::null_mut();
        let status = unsafe { sys::slang_create_global_session(&raw_desc, &mut raw) };
        if !is_success(status) {
            if !raw.is_null() {
                unsafe { sys::slang_global_session_destroy(raw) };
            }
            return Err(Error::from_status(status));
        }
        let Some(raw) = ptr::NonNull::new(raw) else {
            return Err(Error::from_status(sys::SLANG_FAIL));
        };
        Ok(Self {
            inner: Rc::new(GlobalSessionInner { raw }),
        })
    }

    /// Returns Slang's build tag, if the native library provides one.
    pub fn build_tag(&self) -> Option<String> {
        let raw = unsafe { sys::slang_global_session_get_build_tag(self.inner.raw.as_ptr()) };
        if raw.is_null() {
            None
        } else {
            Some(
                unsafe { CStr::from_ptr(raw) }
                    .to_string_lossy()
                    .into_owned(),
            )
        }
    }

    /// Finds a profile by its Slang name, returning `None` for an unknown
    /// profile or an input containing an interior NUL byte.
    pub fn find_profile(&self, name: &str) -> Option<sys::SlangProfileID> {
        let name = CString::new(name).ok()?;
        let profile = unsafe {
            sys::slang_global_session_find_profile(self.inner.raw.as_ptr(), name.as_ptr())
        };
        (profile != sys::SLANG_PROFILE_UNKNOWN).then_some(profile)
    }

    /// Checks whether a target is supported by this Slang build.
    pub fn check_compile_target_support(&self, target: sys::SlangCompileTarget) -> Result<()> {
        let status = unsafe {
            sys::slang_global_session_check_compile_target_support(self.inner.raw.as_ptr(), target)
        };
        if is_success(status) {
            Ok(())
        } else {
            Err(Error::from_status(status))
        }
    }

    /// Creates a session. Descriptor-owned strings and arrays are kept alive
    /// for the complete native call, while Slang retains only its own
    /// references after returning.
    pub fn create_session(&self, desc: &SessionDesc<'_>) -> Result<Session> {
        let mut raw_target_options = Vec::with_capacity(desc.targets.len());
        for target in &desc.targets {
            raw_target_options.push(RawCompilerOptions::new(&target.compiler_options)?);
        }
        let raw_targets: Vec<_> = desc
            .targets
            .iter()
            .zip(&raw_target_options)
            .map(|(target, options)| target.to_raw(options))
            .collect();

        let mut search_paths = Vec::with_capacity(desc.search_paths.len());
        for path in &desc.search_paths {
            search_paths.push(make_cstring(path)?);
        }
        let search_path_ptrs: Vec<_> = search_paths.iter().map(|path| path.as_ptr()).collect();

        let mut macro_names = Vec::with_capacity(desc.preprocessor_macros.len());
        let mut macro_values = Vec::with_capacity(desc.preprocessor_macros.len());
        for (name, value) in &desc.preprocessor_macros {
            macro_names.push(make_cstring(name)?);
            macro_values.push(make_cstring(value)?);
        }
        let raw_macros: Vec<_> = macro_names
            .iter()
            .zip(&macro_values)
            .map(|(name, value)| sys::SlangPreprocessorMacroDesc {
                name: name.as_ptr(),
                value: value.as_ptr(),
            })
            .collect();
        let raw_session_options = RawCompilerOptions::new(&desc.compiler_options)?;

        let file_system = desc.file_system.map(|file_system| file_system.raw());
        let vfs_state = desc
            .file_system
            .map(|file_system| file_system.inner.state.clone());
        let raw_desc = sys::SlangSessionDesc {
            structure_size: std::mem::size_of::<sys::SlangSessionDesc>(),
            targets: non_empty_ptr(raw_targets.as_ptr(), raw_targets.len()),
            target_count: count_as_int(raw_targets.len())?,
            flags: desc.flags,
            default_matrix_layout_mode: desc.default_matrix_layout_mode,
            search_paths: non_empty_ptr(search_path_ptrs.as_ptr(), search_path_ptrs.len()),
            search_path_count: count_as_int(search_path_ptrs.len())?,
            preprocessor_macros: non_empty_ptr(raw_macros.as_ptr(), raw_macros.len()),
            preprocessor_macro_count: count_as_int(raw_macros.len())?,
            file_system: file_system.unwrap_or(ptr::null_mut()),
            enable_effect_annotations: desc.enable_effect_annotations as u8,
            allow_glsl_syntax: desc.allow_glsl_syntax as u8,
            _session_bool_padding: [0; 6],
            compiler_option_entries: raw_session_options.as_ptr(),
            compiler_option_entry_count: raw_session_options.len(),
            skip_spirv_validation: desc.skip_spirv_validation as u8,
            _skip_spirv_validation_padding: [0; 3],
        };

        let mut raw = ptr::null_mut();
        let status = unsafe {
            sys::slang_global_session_create_session(self.inner.raw.as_ptr(), &raw_desc, &mut raw)
        };
        if !is_success(status) {
            if !raw.is_null() {
                unsafe { sys::slang_session_destroy(raw) };
            }
            return Err(Error::from_status(status));
        }
        let Some(raw) = ptr::NonNull::new(raw) else {
            return Err(Error::from_status(sys::SLANG_FAIL));
        };
        Ok(Session {
            inner: Rc::new(SessionInner {
                raw,
                _global: self.inner.clone(),
                _vfs_state: vfs_state,
            }),
        })
    }
}

impl Drop for GlobalSessionInner {
    fn drop(&mut self) {
        unsafe { sys::slang_global_session_destroy(self.raw.as_ptr()) };
    }
}

pub(crate) struct SessionInner {
    pub(crate) raw: ptr::NonNull<sys::ISession>,
    pub(crate) _global: Rc<GlobalSessionInner>,
    pub(crate) _vfs_state: Option<Arc<dyn VfsKeepAlive>>,
}

/// A Slang compilation session.
#[derive(Clone)]
pub struct Session {
    pub(crate) inner: Rc<SessionInner>,
}

impl Session {
    /// Loads and compiles a module from source bytes.
    pub fn load_module_from_source(
        &self,
        module_name: &str,
        path: &str,
        source: &[u8],
    ) -> Result<Output<ComponentType>> {
        let module_name = make_cstring(module_name)?;
        let path = make_cstring(path)?;
        let source = RawBlob::new(source)?;
        let mut diagnostics = ptr::null_mut();
        let mut raw_module = ptr::null_mut();
        let status = unsafe {
            sys::slang_session_load_module_from_source(
                self.inner.raw.as_ptr(),
                module_name.as_ptr(),
                path.as_ptr(),
                source.as_ptr(),
                &mut diagnostics,
                &mut raw_module,
            )
        };

        let Some(raw_module) = ptr::NonNull::new(raw_module.cast::<sys::IComponentType>()) else {
            return Err(Error::from_status_and_diagnostics(
                if is_success(status) {
                    sys::SLANG_FAIL
                } else {
                    status
                },
                crate::error::diagnostics_from_raw(diagnostics),
            ));
        };
        if !is_success(status) {
            unsafe { sys::slang_component_type_destroy(raw_module.as_ptr()) };
            return Err(Error::from_status_and_diagnostics(
                status,
                crate::error::diagnostics_from_raw(diagnostics),
            ));
        }
        finish(
            status,
            ComponentType::from_raw(raw_module, self.inner.clone(), ComponentKind::Module),
            diagnostics,
        )
    }

    /// Creates a composite component from modules, entry points, or already
    /// composed components.
    pub fn create_composite_component_type(
        &self,
        components: &[&ComponentType],
    ) -> Result<Output<ComponentType>> {
        let raw_components: Vec<_> = components
            .iter()
            .map(|component| component.inner.raw.as_ptr())
            .collect();
        if components
            .iter()
            .any(|component| !Rc::ptr_eq(&component.inner.session, &self.inner))
        {
            return Err(Error::invalid_argument());
        }
        let mut raw_component = ptr::null_mut();
        let mut diagnostics = ptr::null_mut();
        let status = unsafe {
            sys::slang_session_create_composite_component_type(
                self.inner.raw.as_ptr(),
                non_empty_ptr(raw_components.as_ptr(), raw_components.len()),
                count_as_int(raw_components.len())?,
                &mut raw_component,
                &mut diagnostics,
            )
        };
        let Some(raw_component) = ptr::NonNull::new(raw_component) else {
            return Err(Error::from_status_and_diagnostics(
                if is_success(status) {
                    sys::SLANG_FAIL
                } else {
                    status
                },
                crate::error::diagnostics_from_raw(diagnostics),
            ));
        };
        if !is_success(status) {
            unsafe { sys::slang_component_type_destroy(raw_component.as_ptr()) };
            return Err(Error::from_status_and_diagnostics(
                status,
                crate::error::diagnostics_from_raw(diagnostics),
            ));
        }
        finish(
            status,
            ComponentType::from_raw(raw_component, self.inner.clone(), ComponentKind::Composite),
            diagnostics,
        )
    }
}

impl Drop for SessionInner {
    fn drop(&mut self) {
        // `_vfs_state` is dropped after this destructor body, so callback
        // state remains valid while Slang releases the session and any
        // internally retained file-system reference.
        unsafe { sys::slang_session_destroy(self.raw.as_ptr()) };
    }
}

fn make_cstring(value: &str) -> Result<CString> {
    CString::new(value).map_err(|_| Error::invalid_argument())
}

fn count_as_int(value: usize) -> Result<sys::SlangInt> {
    sys::SlangInt::try_from(value).map_err(|_| Error::invalid_argument())
}

fn non_empty_ptr<T>(pointer: *const T, len: usize) -> *const T {
    if len == 0 { ptr::null() } else { pointer }
}
