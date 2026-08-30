//! Safe Rust bindings for the focused Slang C ABI.
//!
//! The wrapper follows Slang's object flow: create a global session, create a
//! session, load a module, find entry points, compose/link components, and
//! request target code or reflection. Native handles remain private and are
//! released when the corresponding Rust value is dropped.
//!
//! Slang calls are synchronous. The wrapper does not create worker threads or
//! add synchronization around the native library; callers must follow Slang's
//! synchronization requirements when sharing objects between threads. Native
//! handle wrappers are deliberately not `Send`/`Sync`; the VFS callback state
//! is kept separately and must implement `Send + Sync` because Slang may call
//! it from its own execution context.

mod component;
mod error;
mod session;
mod vfs;

pub use component::{ComponentType, EntryPoint, LinkedComponentType, Module, ProgramLayout};
pub use error::{Diagnostics, Error, Output, Result};
pub use session::{
    CompilerOption, GlobalSession, GlobalSessionDesc, Session, SessionDesc, TargetDesc,
};
pub use vfs::{FileSystem, VirtualFileSystem};

/// The raw crate is re-exported so target, stage, profile, and compiler
/// option constants remain available with the same numeric values as Slang.
pub use slang_slim_sys as sys;

pub use sys::{
    SlangCompileTarget, SlangFloatingPointMode, SlangInt, SlangLineDirectiveMode,
    SlangMatrixLayoutMode, SlangProfileID, SlangResult, SlangStage, SlangTargetFlags,
};

pub use sys::{
    SLANG_HLSL, SLANG_METAL, SLANG_SPIRV, SLANG_STAGE_COMPUTE, SLANG_STAGE_FRAGMENT,
    SLANG_STAGE_VERTEX,
};
