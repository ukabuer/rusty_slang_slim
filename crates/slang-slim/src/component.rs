use std::{
    ffi::{CStr, CString},
    ptr,
    rc::Rc,
};

use slang_slim_sys as sys;

use crate::{
    Error, Output, Result,
    error::{diagnostics_from_raw, finish, is_success, take_blob},
    session::SessionInner,
};

pub(crate) struct ComponentInner {
    pub(crate) raw: ptr::NonNull<sys::IComponentType>,
    pub(crate) session: Rc<SessionInner>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum ComponentKind {
    Module,
    EntryPoint,
    Composite,
    Linked,
}

/// A Slang module, entry point, composite component, or linked component.
///
/// Slang exposes all of these through `IComponentType` handles. The safe
/// wrapper keeps one Rust type for that common interface, while the aliases
/// [`Module`], [`EntryPoint`], and [`LinkedComponentType`] preserve the names
/// used in Slang's C++ workflow.
#[derive(Clone)]
pub struct ComponentType {
    pub(crate) inner: Rc<ComponentInner>,
    pub(crate) kind: ComponentKind,
}

/// Alias matching Slang's module object in the native API.
pub type Module = ComponentType;
/// Alias matching Slang's entry-point object in the native API.
pub type EntryPoint = ComponentType;
/// Alias for the component returned by `link`.
pub type LinkedComponentType = ComponentType;

impl ComponentType {
    pub(crate) fn from_raw(
        raw: ptr::NonNull<sys::IComponentType>,
        session: Rc<SessionInner>,
        kind: ComponentKind,
    ) -> Self {
        Self {
            inner: Rc::new(ComponentInner { raw, session }),
            kind,
        }
    }

    /// Finds and validates an entry point in this module.
    pub fn find_and_check_entry_point(
        &self,
        name: &str,
        stage: sys::SlangStage,
    ) -> Result<Output<EntryPoint>> {
        if self.kind != ComponentKind::Module {
            return Err(Error::invalid_argument());
        }
        let name = CString::new(name).map_err(|_| Error::invalid_argument())?;
        let mut raw_entry_point = ptr::null_mut();
        let mut diagnostics = ptr::null_mut();
        let status = unsafe {
            sys::slang_module_find_and_check_entry_point(
                self.inner.raw.as_ptr().cast::<sys::IModule>(),
                name.as_ptr(),
                stage,
                &mut raw_entry_point,
                &mut diagnostics,
            )
        };

        let Some(raw_entry_point) =
            ptr::NonNull::new(raw_entry_point.cast::<sys::IComponentType>())
        else {
            let diagnostics = diagnostics_from_raw(diagnostics);
            return Err(Error::from_status_and_diagnostics(
                if is_success(status) {
                    sys::SLANG_FAIL
                } else {
                    status
                },
                diagnostics,
            ));
        };
        if !is_success(status) {
            unsafe { sys::slang_component_type_destroy(raw_entry_point.as_ptr()) };
            return Err(Error::from_status_and_diagnostics(
                status,
                diagnostics_from_raw(diagnostics),
            ));
        }

        finish(
            status,
            ComponentType::from_raw(
                raw_entry_point,
                self.inner.session.clone(),
                ComponentKind::EntryPoint,
            ),
            diagnostics,
        )
    }

    /// Returns the module name copied into an owned Rust string.
    pub fn name(&self) -> Option<String> {
        if self.kind != ComponentKind::Module {
            return None;
        }
        let name =
            unsafe { sys::slang_module_get_name(self.inner.raw.as_ptr().cast::<sys::IModule>()) };
        c_string(name)
    }

    /// Returns the source file path copied into an owned Rust string.
    pub fn file_path(&self) -> Option<String> {
        if self.kind != ComponentKind::Module {
            return None;
        }
        let path = unsafe {
            sys::slang_module_get_file_path(self.inner.raw.as_ptr().cast::<sys::IModule>())
        };
        c_string(path)
    }

    /// Links this component graph and returns the linked component.
    pub fn link(&self) -> Result<Output<LinkedComponentType>> {
        let mut raw_linked = ptr::null_mut();
        let mut diagnostics = ptr::null_mut();
        let status = unsafe {
            sys::slang_component_type_link(
                self.inner.raw.as_ptr(),
                &mut raw_linked,
                &mut diagnostics,
            )
        };
        let Some(raw_linked) = ptr::NonNull::new(raw_linked) else {
            return Err(Error::from_status_and_diagnostics(
                if is_success(status) {
                    sys::SLANG_FAIL
                } else {
                    status
                },
                diagnostics_from_raw(diagnostics),
            ));
        };
        if !is_success(status) {
            unsafe { sys::slang_component_type_destroy(raw_linked.as_ptr()) };
            return Err(Error::from_status_and_diagnostics(
                status,
                diagnostics_from_raw(diagnostics),
            ));
        }

        finish(
            status,
            ComponentType::from_raw(
                raw_linked,
                self.inner.session.clone(),
                ComponentKind::Linked,
            ),
            diagnostics,
        )
    }

    /// Returns complete target code for this component graph.
    pub fn get_target_code(&self, target_index: sys::SlangInt) -> Result<Output<Vec<u8>>> {
        let mut raw_code = ptr::null_mut();
        let mut diagnostics = ptr::null_mut();
        let status = unsafe {
            sys::slang_component_type_get_target_code(
                self.inner.raw.as_ptr(),
                target_index,
                &mut raw_code,
                &mut diagnostics,
            )
        };
        let had_code = !raw_code.is_null();
        let code = unsafe { take_blob(raw_code) };
        if !is_success(status) {
            return Err(Error::from_status_and_diagnostics(
                status,
                diagnostics_from_raw(diagnostics),
            ));
        }
        if !had_code {
            return Err(Error::from_status_and_diagnostics(
                sys::SLANG_FAIL,
                diagnostics_from_raw(diagnostics),
            ));
        }
        Ok(Output::new(status, code, diagnostics_from_raw(diagnostics)))
    }

    /// Returns code for one entry point and one target.
    pub fn get_entry_point_code(
        &self,
        entry_point_index: sys::SlangInt,
        target_index: sys::SlangInt,
    ) -> Result<Output<Vec<u8>>> {
        let mut raw_code = ptr::null_mut();
        let mut diagnostics = ptr::null_mut();
        let status = unsafe {
            sys::slang_component_type_get_entry_point_code(
                self.inner.raw.as_ptr(),
                entry_point_index,
                target_index,
                &mut raw_code,
                &mut diagnostics,
            )
        };
        let had_code = !raw_code.is_null();
        let code = unsafe { take_blob(raw_code) };
        if !is_success(status) {
            return Err(Error::from_status_and_diagnostics(
                status,
                diagnostics_from_raw(diagnostics),
            ));
        }
        if !had_code {
            return Err(Error::from_status_and_diagnostics(
                sys::SLANG_FAIL,
                diagnostics_from_raw(diagnostics),
            ));
        }
        Ok(Output::new(status, code, diagnostics_from_raw(diagnostics)))
    }

    /// Returns the reflection layout for one target.
    pub fn get_layout(&self, target_index: sys::SlangInt) -> Result<Output<ProgramLayout>> {
        let mut raw_layout = ptr::null_mut();
        let mut diagnostics = ptr::null_mut();
        let status = unsafe {
            sys::slang_component_type_get_layout(
                self.inner.raw.as_ptr(),
                target_index,
                &mut raw_layout,
                &mut diagnostics,
            )
        };
        let Some(raw_layout) = ptr::NonNull::new(raw_layout) else {
            return Err(Error::from_status_and_diagnostics(
                if is_success(status) {
                    sys::SLANG_FAIL
                } else {
                    status
                },
                diagnostics_from_raw(diagnostics),
            ));
        };
        if !is_success(status) {
            unsafe { sys::slang_program_layout_destroy(raw_layout.as_ptr()) };
            return Err(Error::from_status_and_diagnostics(
                status,
                diagnostics_from_raw(diagnostics),
            ));
        }

        let layout = ProgramLayout {
            inner: Rc::new(ProgramLayoutInner {
                raw: raw_layout,
                _owner: self.inner.clone(),
            }),
        };
        finish(status, layout, diagnostics)
    }
}

struct ProgramLayoutInner {
    raw: ptr::NonNull<sys::ProgramLayout>,
    // Slang documents layouts as belonging to their component type. Keeping
    // the owner alive prevents the native layout from outliving its graph.
    _owner: Rc<ComponentInner>,
}

/// Reflection layout for a component graph.
#[derive(Clone)]
pub struct ProgramLayout {
    inner: Rc<ProgramLayoutInner>,
}

impl ProgramLayout {
    /// Serializes this layout using Slang's JSON reflection format.
    pub fn to_json(&self) -> Result<Output<Vec<u8>>> {
        let mut raw_json = ptr::null_mut();
        let status =
            unsafe { sys::slang_program_layout_to_json(self.inner.raw.as_ptr(), &mut raw_json) };
        let had_json = !raw_json.is_null();
        let json = unsafe { take_blob(raw_json) };
        if !is_success(status) {
            return Err(Error::from_status(status));
        }
        if !had_json {
            return Err(Error::from_status(sys::SLANG_FAIL));
        }
        Ok(Output::new(status, json, Default::default()))
    }

    /// Serializes this layout and decodes it as UTF-8 with replacement for
    /// malformed bytes.
    pub fn to_json_string(&self) -> Result<Output<String>> {
        let output = self.to_json()?;
        Ok(Output::new(
            output.status,
            String::from_utf8_lossy(&output.value).into_owned(),
            output.diagnostics,
        ))
    }
}

impl Drop for ComponentInner {
    fn drop(&mut self) {
        unsafe { sys::slang_component_type_destroy(self.raw.as_ptr()) };
    }
}

impl Drop for ProgramLayoutInner {
    fn drop(&mut self) {
        unsafe { sys::slang_program_layout_destroy(self.raw.as_ptr()) };
    }
}

fn c_string(value: *const std::ffi::c_char) -> Option<String> {
    if value.is_null() {
        None
    } else {
        Some(
            unsafe { CStr::from_ptr(value) }
                .to_string_lossy()
                .into_owned(),
        )
    }
}
