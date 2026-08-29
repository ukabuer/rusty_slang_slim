fn main() {
    // Release-asset selection and native linker directives are added once the
    // GitHub asset naming/checksum contract is finalized. Keeping this script
    // link-free lets the raw declarations be checked in a source-only checkout.
    println!("cargo::rerun-if-changed=build.rs");
    println!("cargo::rerun-if-changed=../../native/include/slang_slim.h");
}
