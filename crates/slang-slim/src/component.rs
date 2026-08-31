use std::{
    ffi::{CStr, CString},
    ops::Deref,
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

/// The common safe view of Slang's `IComponentType` interface.
///
/// Slang's module, entry-point, composite, and linked objects all implement
/// `IComponentType`. The safe wrapper keeps the common operations on this type
/// and exposes the more specific object names as distinct wrappers below.
#[derive(Clone)]
pub struct ComponentType {
    pub(crate) inner: Rc<ComponentInner>,
}

/// A module loaded into a [`crate::Session`].
#[derive(Clone)]
pub struct Module {
    pub(crate) component: ComponentType,
}

/// An entry point discovered from a [`Module`].
#[derive(Clone)]
pub struct EntryPoint {
    pub(crate) component: ComponentType,
}

/// A component graph returned by [`crate::Session::create_composite_component_type`].
#[derive(Clone)]
pub struct CompositeComponentType {
    pub(crate) component: ComponentType,
}

/// A fully linked component graph.
#[derive(Clone)]
pub struct LinkedComponentType {
    pub(crate) component: ComponentType,
}

/// A component that can participate in Slang composition.
///
/// The trait is intentionally limited to the common `IComponentType` view
/// needed by `Session::create_composite_component_type`. Concrete wrappers
/// retain their distinct methods and ownership.
pub trait Component {
    fn as_component_type(&self) -> &ComponentType;
}

impl Component for ComponentType {
    fn as_component_type(&self) -> &ComponentType {
        self
    }
}

impl Component for Module {
    fn as_component_type(&self) -> &ComponentType {
        &self.component
    }
}

impl Component for EntryPoint {
    fn as_component_type(&self) -> &ComponentType {
        &self.component
    }
}

impl Component for CompositeComponentType {
    fn as_component_type(&self) -> &ComponentType {
        &self.component
    }
}

impl Component for LinkedComponentType {
    fn as_component_type(&self) -> &ComponentType {
        &self.component
    }
}

macro_rules! impl_component_deref {
    ($type:ty) => {
        impl Deref for $type {
            type Target = ComponentType;

            fn deref(&self) -> &Self::Target {
                &self.component
            }
        }
    };
}

impl_component_deref!(Module);
impl_component_deref!(EntryPoint);
impl_component_deref!(CompositeComponentType);
impl_component_deref!(LinkedComponentType);

impl ComponentType {
    pub(crate) fn from_raw(
        raw: ptr::NonNull<sys::IComponentType>,
        session: Rc<SessionInner>,
    ) -> Self {
        Self {
            inner: Rc::new(ComponentInner { raw, session }),
        }
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
            LinkedComponentType {
                component: ComponentType::from_raw(raw_linked, self.inner.session.clone()),
            },
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

impl Module {
    /// Finds and validates an entry point in this module.
    pub fn find_and_check_entry_point(
        &self,
        name: &str,
        stage: sys::SlangStage,
    ) -> Result<Output<EntryPoint>> {
        let name = CString::new(name).map_err(|_| Error::invalid_argument())?;
        let mut raw_entry_point = ptr::null_mut();
        let mut diagnostics = ptr::null_mut();
        let status = unsafe {
            sys::slang_module_find_and_check_entry_point(
                self.component.inner.raw.as_ptr().cast::<sys::IModule>(),
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
            EntryPoint {
                component: ComponentType::from_raw(
                    raw_entry_point,
                    self.component.inner.session.clone(),
                ),
            },
            diagnostics,
        )
    }

    /// Returns the module name copied into an owned Rust string.
    pub fn name(&self) -> Option<String> {
        let name = unsafe {
            sys::slang_module_get_name(self.component.inner.raw.as_ptr().cast::<sys::IModule>())
        };
        c_string(name)
    }

    /// Returns the source file path copied into an owned Rust string.
    pub fn file_path(&self) -> Option<String> {
        let path = unsafe {
            sys::slang_module_get_file_path(
                self.component.inner.raw.as_ptr().cast::<sys::IModule>(),
            )
        };
        c_string(path)
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
    fn raw_reflection(&self) -> Option<ptr::NonNull<sys::SlangReflection>> {
        ptr::NonNull::new(unsafe {
            sys::slang_program_layout_get_reflection(self.inner.raw.as_ptr())
        })
    }

    /// Returns the number of global shader parameters in this layout.
    pub fn get_parameter_count(&self) -> u32 {
        let Some(reflection) = self.raw_reflection() else {
            return 0;
        };
        unsafe { sys::slang_reflection_get_parameter_count(reflection.as_ptr()) }
    }

    /// Returns one global shader parameter layout by index.
    pub fn get_parameter_by_index(&self, index: u32) -> Option<VariableLayoutReflection> {
        let reflection = self.raw_reflection()?;
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_get_parameter_by_index(reflection.as_ptr(), index)
        })?;
        Some(VariableLayoutReflection {
            raw,
            owner: self.inner.clone(),
        })
    }

    /// Returns the number of entry points in this layout.
    pub fn get_entry_point_count(&self) -> sys::SlangUInt {
        let Some(reflection) = self.raw_reflection() else {
            return 0;
        };
        unsafe { sys::slang_reflection_get_entry_point_count(reflection.as_ptr()) }
    }

    /// Returns one entry-point reflection object by index.
    pub fn get_entry_point_by_index(&self, index: sys::SlangUInt) -> Option<EntryPointReflection> {
        let reflection = self.raw_reflection()?;
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_get_entry_point_by_index(reflection.as_ptr(), index)
        })?;
        Some(EntryPointReflection {
            raw,
            owner: self.inner.clone(),
        })
    }

    /// Finds an entry point by its reflected name.
    pub fn find_entry_point_by_name(&self, name: &str) -> Result<Option<EntryPointReflection>> {
        let name = CString::new(name).map_err(|_| Error::invalid_argument())?;
        let Some(reflection) = self.raw_reflection() else {
            return Ok(None);
        };
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_find_entry_point_by_name(reflection.as_ptr(), name.as_ptr())
        });
        Ok(raw.map(|raw| EntryPointReflection {
            raw,
            owner: self.inner.clone(),
        }))
    }

    /// Finds a reflected shader type by its fully qualified name.
    pub fn find_type_by_name(&self, name: &str) -> Result<Option<TypeReflection>> {
        let name = CString::new(name).map_err(|_| Error::invalid_argument())?;
        let Some(reflection) = self.raw_reflection() else {
            return Ok(None);
        };
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_find_type_by_name(reflection.as_ptr(), name.as_ptr())
        });
        Ok(raw.map(|raw| TypeReflection {
            raw,
            owner: self.inner.clone(),
        }))
    }

    /// Returns layout information for a reflected type under the requested
    /// Slang layout rules.
    pub fn get_type_layout(
        &self,
        reflection_type: &TypeReflection,
        rules: sys::SlangLayoutRules,
    ) -> Result<Option<TypeLayoutReflection>> {
        if !Rc::ptr_eq(&self.inner, &reflection_type.owner) {
            return Err(Error::invalid_argument());
        }
        let Some(reflection) = self.raw_reflection() else {
            return Ok(None);
        };
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_get_type_layout(
                reflection.as_ptr(),
                reflection_type.raw.as_ptr(),
                rules,
            )
        });
        Ok(raw.map(|raw| TypeLayoutReflection {
            raw,
            owner: self.inner.clone(),
        }))
    }

    /// Returns the layout for global-scope parameters.
    pub fn get_global_params_type_layout(&self) -> Option<TypeLayoutReflection> {
        let reflection = self.raw_reflection()?;
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_get_global_params_type_layout(reflection.as_ptr())
        })?;
        Some(TypeLayoutReflection {
            raw,
            owner: self.inner.clone(),
        })
    }

    /// Returns the variable layout for global-scope parameters.
    pub fn get_global_params_var_layout(&self) -> Option<VariableLayoutReflection> {
        let reflection = self.raw_reflection()?;
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_get_global_params_var_layout(reflection.as_ptr())
        })?;
        Some(VariableLayoutReflection {
            raw,
            owner: self.inner.clone(),
        })
    }

    /// Serializes this layout using Slang's JSON reflection format.
    pub fn to_json(&self) -> Result<Output<Vec<u8>>> {
        let Some(reflection) = self.raw_reflection() else {
            return Err(Error::from_status(sys::SLANG_E_INVALID_HANDLE));
        };
        let mut raw_json = ptr::null_mut();
        let status = unsafe { sys::slang_reflection_to_json(reflection.as_ptr(), &mut raw_json) };
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

/// Reflection for one entry point in a [`ProgramLayout`].
#[derive(Clone)]
pub struct EntryPointReflection {
    raw: ptr::NonNull<sys::SlangReflectionEntryPoint>,
    owner: Rc<ProgramLayoutInner>,
}

impl EntryPointReflection {
    /// Returns the reflected entry-point name.
    pub fn get_name(&self) -> Option<String> {
        let name = unsafe { sys::slang_reflection_entry_point_get_name(self.raw.as_ptr()) };
        c_string(name)
    }

    /// Returns the Slang stage associated with this entry point.
    pub fn get_stage(&self) -> sys::SlangStage {
        unsafe { sys::slang_reflection_entry_point_get_stage(self.raw.as_ptr()) }
    }

    /// Returns the number of reflected entry-point parameters.
    pub fn get_parameter_count(&self) -> u32 {
        unsafe { sys::slang_reflection_entry_point_get_parameter_count(self.raw.as_ptr()) }
    }

    /// Returns one reflected entry-point parameter by index.
    pub fn get_parameter_by_index(&self, index: u32) -> Option<VariableLayoutReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_entry_point_get_parameter_by_index(self.raw.as_ptr(), index)
        })?;
        Some(VariableLayoutReflection {
            raw,
            owner: self.owner.clone(),
        })
    }

    /// Returns the reflected layout of the entry-point parameter block.
    pub fn get_var_layout(&self) -> Option<VariableLayoutReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_entry_point_get_var_layout(self.raw.as_ptr())
        })?;
        Some(VariableLayoutReflection {
            raw,
            owner: self.owner.clone(),
        })
    }

    /// Returns the reflected layout of the entry-point result variable.
    pub fn get_result_var_layout(&self) -> Option<VariableLayoutReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_entry_point_get_result_var_layout(self.raw.as_ptr())
        })?;
        Some(VariableLayoutReflection {
            raw,
            owner: self.owner.clone(),
        })
    }

    /// Returns the compute thread-group dimensions for the requested number
    /// of axes.
    pub fn get_compute_thread_group_size(&self, axis_count: usize) -> Result<Vec<sys::SlangUInt>> {
        let axis_count_u64 =
            sys::SlangUInt::try_from(axis_count).map_err(|_| Error::invalid_argument())?;
        let mut sizes = vec![0; axis_count];
        unsafe {
            sys::slang_reflection_entry_point_get_compute_thread_group_size(
                self.raw.as_ptr(),
                axis_count_u64,
                sizes.as_mut_ptr(),
            );
        }
        Ok(sizes)
    }
}

/// Reflection for a Slang type declaration.
#[derive(Clone)]
pub struct TypeReflection {
    raw: ptr::NonNull<sys::SlangReflectionType>,
    owner: Rc<ProgramLayoutInner>,
}

impl TypeReflection {
    /// Returns the Slang type kind.
    pub fn get_kind(&self) -> sys::SlangTypeKind {
        unsafe { sys::slang_reflection_type_get_kind(self.raw.as_ptr()) }
    }

    /// Returns the reflected type name.
    pub fn get_name(&self) -> Option<String> {
        let name = unsafe { sys::slang_reflection_type_get_name(self.raw.as_ptr()) };
        c_string(name)
    }

    /// Returns the number of fields for a struct type.
    pub fn get_field_count(&self) -> u32 {
        unsafe { sys::slang_reflection_type_get_field_count(self.raw.as_ptr()) }
    }

    /// Returns one struct field by index.
    pub fn get_field_by_index(&self, index: u32) -> Option<VariableReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_type_get_field_by_index(self.raw.as_ptr(), index)
        })?;
        Some(VariableReflection {
            raw,
            owner: self.owner.clone(),
        })
    }

    /// Returns the number of elements in an array or vector type.
    pub fn get_element_count(&self) -> usize {
        unsafe { sys::slang_reflection_type_get_element_count(self.raw.as_ptr()) }
    }

    /// Returns the element type for an array or vector type.
    pub fn get_element_type(&self) -> Option<TypeReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_type_get_element_type(self.raw.as_ptr())
        })?;
        Some(TypeReflection {
            raw,
            owner: self.owner.clone(),
        })
    }

    /// Returns the row count for a matrix type.
    pub fn get_row_count(&self) -> u32 {
        unsafe { sys::slang_reflection_type_get_row_count(self.raw.as_ptr()) }
    }

    /// Returns the column count for a matrix or vector type.
    pub fn get_column_count(&self) -> u32 {
        unsafe { sys::slang_reflection_type_get_column_count(self.raw.as_ptr()) }
    }

    /// Returns the scalar type for a scalar, vector, or matrix type.
    pub fn get_scalar_type(&self) -> sys::SlangScalarType {
        unsafe { sys::slang_reflection_type_get_scalar_type(self.raw.as_ptr()) }
    }

    /// Returns the resource shape for a resource type.
    pub fn get_resource_shape(&self) -> sys::SlangResourceShape {
        unsafe { sys::slang_reflection_type_get_resource_shape(self.raw.as_ptr()) }
    }

    /// Returns the resource access mode for a resource type.
    pub fn get_resource_access(&self) -> sys::SlangResourceAccess {
        unsafe { sys::slang_reflection_type_get_resource_access(self.raw.as_ptr()) }
    }

    /// Returns the result type for a resource type.
    pub fn get_resource_result_type(&self) -> Option<TypeReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_type_get_resource_result_type(self.raw.as_ptr())
        })?;
        Some(TypeReflection {
            raw,
            owner: self.owner.clone(),
        })
    }
}

/// Target-specific layout information for a reflected type.
#[derive(Clone)]
pub struct TypeLayoutReflection {
    raw: ptr::NonNull<sys::SlangReflectionTypeLayout>,
    owner: Rc<ProgramLayoutInner>,
}

impl TypeLayoutReflection {
    /// Returns the reflected type represented by this layout.
    pub fn get_type(&self) -> Option<TypeReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_type_layout_get_type(self.raw.as_ptr())
        })?;
        Some(TypeReflection {
            raw,
            owner: self.owner.clone(),
        })
    }

    /// Returns the Slang type kind.
    pub fn get_kind(&self) -> sys::SlangTypeKind {
        unsafe { sys::slang_reflection_type_layout_get_kind(self.raw.as_ptr()) }
    }

    /// Returns the size in the requested parameter category.
    pub fn get_size(&self, category: sys::SlangParameterCategory) -> usize {
        unsafe { sys::slang_reflection_type_layout_get_size(self.raw.as_ptr(), category) }
    }

    /// Returns the stride in the requested parameter category.
    pub fn get_stride(&self, category: sys::SlangParameterCategory) -> usize {
        unsafe { sys::slang_reflection_type_layout_get_stride(self.raw.as_ptr(), category) }
    }

    /// Returns the alignment in the requested parameter category.
    pub fn get_alignment(&self, category: sys::SlangParameterCategory) -> i32 {
        unsafe { sys::slang_reflection_type_layout_get_alignment(self.raw.as_ptr(), category) }
    }

    /// Returns the number of fields in a struct layout.
    pub fn get_field_count(&self) -> u32 {
        unsafe { sys::slang_reflection_type_layout_get_field_count(self.raw.as_ptr()) }
    }

    /// Returns one field layout by index.
    pub fn get_field_by_index(&self, index: u32) -> Option<VariableLayoutReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_type_layout_get_field_by_index(self.raw.as_ptr(), index)
        })?;
        Some(VariableLayoutReflection {
            raw,
            owner: self.owner.clone(),
        })
    }

    /// Returns the element stride in the requested parameter category.
    pub fn get_element_stride(&self, category: sys::SlangParameterCategory) -> usize {
        unsafe { sys::slang_reflection_type_layout_get_element_stride(self.raw.as_ptr(), category) }
    }

    /// Returns the element type layout for an array or vector layout.
    pub fn get_element_type_layout(&self) -> Option<TypeLayoutReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_type_layout_get_element_type_layout(self.raw.as_ptr())
        })?;
        Some(TypeLayoutReflection {
            raw,
            owner: self.owner.clone(),
        })
    }

    /// Returns the element variable layout for an array or vector layout.
    pub fn get_element_var_layout(&self) -> Option<VariableLayoutReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_type_layout_get_element_var_layout(self.raw.as_ptr())
        })?;
        Some(VariableLayoutReflection {
            raw,
            owner: self.owner.clone(),
        })
    }

    /// Returns the variable layout that contains this type layout.
    pub fn get_container_var_layout(&self) -> Option<VariableLayoutReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_type_layout_get_container_var_layout(self.raw.as_ptr())
        })?;
        Some(VariableLayoutReflection {
            raw,
            owner: self.owner.clone(),
        })
    }

    /// Returns the primary parameter category for this layout.
    pub fn get_parameter_category(&self) -> sys::SlangParameterCategory {
        unsafe { sys::slang_reflection_type_layout_get_parameter_category(self.raw.as_ptr()) }
    }

    /// Returns the matrix layout mode used by this type layout.
    pub fn get_matrix_layout_mode(&self) -> sys::SlangMatrixLayoutMode {
        unsafe { sys::slang_reflection_type_layout_get_matrix_layout_mode(self.raw.as_ptr()) }
    }
}

/// Reflection for a variable declaration.
#[derive(Clone)]
pub struct VariableReflection {
    raw: ptr::NonNull<sys::SlangReflectionVariable>,
    owner: Rc<ProgramLayoutInner>,
}

impl VariableReflection {
    /// Returns the reflected variable name.
    pub fn get_name(&self) -> Option<String> {
        let name = unsafe { sys::slang_reflection_variable_get_name(self.raw.as_ptr()) };
        c_string(name)
    }

    /// Returns the reflected variable type.
    pub fn get_type(&self) -> Option<TypeReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_variable_get_type(self.raw.as_ptr())
        })?;
        Some(TypeReflection {
            raw,
            owner: self.owner.clone(),
        })
    }
}

/// Target-specific layout information for a reflected variable.
#[derive(Clone)]
pub struct VariableLayoutReflection {
    raw: ptr::NonNull<sys::SlangReflectionVariableLayout>,
    owner: Rc<ProgramLayoutInner>,
}

impl VariableLayoutReflection {
    /// Returns the reflected variable declaration.
    pub fn get_variable(&self) -> Option<VariableReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_variable_layout_get_variable(self.raw.as_ptr())
        })?;
        Some(VariableReflection {
            raw,
            owner: self.owner.clone(),
        })
    }

    /// Returns the target-specific type layout.
    pub fn get_type_layout(&self) -> Option<TypeLayoutReflection> {
        let raw = ptr::NonNull::new(unsafe {
            sys::slang_reflection_variable_layout_get_type_layout(self.raw.as_ptr())
        })?;
        Some(TypeLayoutReflection {
            raw,
            owner: self.owner.clone(),
        })
    }

    /// Returns the offset in the requested parameter category.
    pub fn get_offset(&self, category: sys::SlangParameterCategory) -> usize {
        unsafe { sys::slang_reflection_variable_layout_get_offset(self.raw.as_ptr(), category) }
    }

    /// Returns the register space/set in the requested parameter category.
    pub fn get_space(&self, category: sys::SlangParameterCategory) -> usize {
        unsafe { sys::slang_reflection_variable_layout_get_space(self.raw.as_ptr(), category) }
    }

    /// Returns the semantic name, if one is present.
    pub fn get_semantic_name(&self) -> Option<String> {
        let name =
            unsafe { sys::slang_reflection_variable_layout_get_semantic_name(self.raw.as_ptr()) };
        c_string(name)
    }

    /// Returns the semantic index.
    pub fn get_semantic_index(&self) -> usize {
        unsafe { sys::slang_reflection_variable_layout_get_semantic_index(self.raw.as_ptr()) }
    }

    /// Returns the stage associated with this variable, if any.
    pub fn get_stage(&self) -> sys::SlangStage {
        unsafe { sys::slang_reflection_variable_layout_get_stage(self.raw.as_ptr()) }
    }

    /// Returns the register/binding index for a shader parameter.
    pub fn get_binding_index(&self) -> u32 {
        unsafe {
            sys::slang_reflection_parameter_get_binding_index(
                self.raw.as_ptr().cast::<sys::SlangReflectionParameter>(),
            )
        }
    }

    /// Returns the register space/set for a shader parameter.
    pub fn get_binding_space(&self) -> u32 {
        unsafe {
            sys::slang_reflection_parameter_get_binding_space(
                self.raw.as_ptr().cast::<sys::SlangReflectionParameter>(),
            )
        }
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
