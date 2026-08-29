fn main() {
    // Native artifact selection and linking will be added after the native
    // feasibility spike establishes the final archive layout.
    println!("cargo::rerun-if-changed=build.rs");
}
