use fs2::FileExt;
use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::collections::HashSet;
use std::env;
use std::error::Error;
use std::ffi::OsString;
use std::fs::{self, File, OpenOptions};
use std::io::{self, BufReader, Read, Write};
use std::path::{Component, Path, PathBuf};

const INDEX_JSON: &str = include_str!("native-artifacts.json");
const MANIFEST_SCHEMA_VERSION: u32 = 1;
const ABI_VERSION: u32 = 1;
const DEVELOPMENT_VERSION: &str = "0.0.0";
const SUPPORTED_TARGETS: [&str; 2] = ["x86_64-pc-windows-msvc", "aarch64-linux-android"];

const ENV_NATIVE_DIR: &str = "SLANG_SLIM_NATIVE_DIR";
const ENV_NATIVE_ARCHIVE: &str = "SLANG_SLIM_NATIVE_ARCHIVE";
const ENV_NATIVE_SHA256: &str = "SLANG_SLIM_NATIVE_SHA256";
const ENV_CACHE_DIR: &str = "SLANG_SLIM_CACHE_DIR";
const ENV_RELEASE_BASE_URL: &str = "SLANG_SLIM_RELEASE_BASE_URL";
const ENV_DISABLE_DOWNLOAD: &str = "SLANG_SLIM_DISABLE_DOWNLOAD";

type BuildResult<T> = Result<T, Box<dyn Error + Send + Sync>>;

#[derive(Debug, Deserialize)]
struct ArtifactIndex {
    schema_version: u32,
    release_base_url: Option<String>,
    artifacts: Vec<ReleaseArtifact>,
}

#[derive(Debug, Deserialize)]
struct ReleaseArtifact {
    version: String,
    target: String,
    archive: String,
    sha256: String,
}

#[derive(Debug, Deserialize)]
struct NativeManifest {
    schema_version: u32,
    package: String,
    version: String,
    abi_version: u32,
    target: String,
    link: LinkManifest,
    files: Vec<FileManifest>,
}

#[derive(Debug, Deserialize)]
struct LinkManifest {
    kind: String,
    search_path: String,
    libraries: Vec<LibraryManifest>,
    runtime_libraries: Vec<String>,
    system_libraries: Vec<String>,
    arguments: Vec<String>,
}

#[derive(Debug, Deserialize)]
struct LibraryManifest {
    name: String,
    path: String,
}

#[derive(Debug, Deserialize)]
struct FileManifest {
    path: String,
    size: u64,
    sha256: String,
}

fn main() {
    println!("cargo::rerun-if-changed=build.rs");
    println!("cargo::rerun-if-changed=native-artifacts.json");
    println!("cargo::rerun-if-changed=../../native/include/slang_slim.h");
    println!("cargo::rustc-check-cfg=cfg(slang_slim_native_linked)");
    for variable in [
        ENV_NATIVE_DIR,
        ENV_NATIVE_ARCHIVE,
        ENV_NATIVE_SHA256,
        ENV_CACHE_DIR,
        ENV_RELEASE_BASE_URL,
        ENV_DISABLE_DOWNLOAD,
        "CARGO_NET_OFFLINE",
        "CARGO_HOME",
        "CARGO_CFG_TARGET_FEATURE",
        "DOCS_RS",
    ] {
        println!("cargo::rerun-if-env-changed={variable}");
    }

    if let Err(error) = run() {
        panic!("slang-slim-sys native setup failed: {error}");
    }
}

fn run() -> BuildResult<()> {
    if env::var_os("DOCS_RS").is_some() {
        println!("cargo::warning=slang-slim-sys skips native linking on docs.rs");
        return Ok(());
    }

    let target = required_env("TARGET")?;
    let version = required_env("CARGO_PKG_VERSION")?;
    let index: ArtifactIndex = serde_json::from_str(INDEX_JSON)?;
    if index.schema_version != MANIFEST_SCHEMA_VERSION {
        return Err(format!(
            "unsupported native-artifacts.json schema {}; expected {}",
            index.schema_version, MANIFEST_SCHEMA_VERSION
        )
        .into());
    }

    let release = index
        .artifacts
        .iter()
        .find(|artifact| artifact.version == version && artifact.target == target);
    let native_dir = env::var_os(ENV_NATIVE_DIR);
    let native_archive = env::var_os(ENV_NATIVE_ARCHIVE);
    if native_dir.is_some() && native_archive.is_some() {
        return Err(format!("set only one of {ENV_NATIVE_DIR} and {ENV_NATIVE_ARCHIVE}").into());
    }

    if version == DEVELOPMENT_VERSION
        && release.is_none()
        && native_dir.is_none()
        && native_archive.is_none()
    {
        println!(
            "cargo::warning=slang-slim-sys {DEVELOPMENT_VERSION} is source-only; set \
             {ENV_NATIVE_ARCHIVE} or {ENV_NATIVE_DIR} to exercise native linking"
        );
        return Ok(());
    }

    if !SUPPORTED_TARGETS.contains(&target.as_str()) {
        return Err(format!(
            "unsupported Rust target {target}; supported targets: {}",
            SUPPORTED_TARGETS.join(", ")
        )
        .into());
    }
    ensure_supported_runtime(&target)?;

    let native_root = if let Some(path) = native_dir {
        let path = resolve_user_path(path)?;
        println!(
            "cargo::rerun-if-changed={}",
            path.join("manifest.json").display()
        );
        path
    } else {
        let archive_source = native_archive.map(resolve_user_path).transpose()?;
        if let Some(path) = archive_source.as_ref() {
            println!("cargo::rerun-if-changed={}", path.display());
        }
        let expected_hash = expected_archive_hash(archive_source.as_deref(), release)?;
        let archive_name = release
            .map(|artifact| artifact.archive.clone())
            .or_else(|| {
                archive_source
                    .as_ref()
                    .and_then(|path| path.file_name())
                    .map(|name| name.to_string_lossy().into_owned())
            })
            .ok_or("could not determine native archive name")?;
        let cache_root = cache_root()?;
        fs::create_dir_all(&cache_root)?;
        let lock_path = cache_root.join(format!("{target}.lock"));
        let lock = OpenOptions::new()
            .create(true)
            .truncate(false)
            .read(true)
            .write(true)
            .open(&lock_path)?;
        FileExt::lock_exclusive(&lock)?;

        let cached_archive = ensure_cached_archive(
            &cache_root,
            &archive_name,
            &expected_hash,
            archive_source.as_deref(),
            release,
            index.release_base_url.as_deref(),
            &version,
        )?;
        ensure_extracted(
            &cache_root,
            &cached_archive,
            &expected_hash,
            &target,
            &version,
        )?
    };

    let manifest = validate_native_root(&native_root, &target, &version)?;
    emit_link_instructions(&native_root, &manifest)?;
    Ok(())
}

fn ensure_supported_runtime(target: &str) -> BuildResult<()> {
    if target != "x86_64-pc-windows-msvc" {
        return Ok(());
    }
    let target_features = env::var("CARGO_CFG_TARGET_FEATURE").unwrap_or_default();
    if target_features
        .split(',')
        .any(|feature| feature.trim() == "crt-static")
    {
        return Err(
            ("Windows native assets currently use the dynamic MSVC CRT (/MD); remove Rust "
                .to_owned()
                + "target feature `crt-static` or use a future static-CRT asset")
                .into(),
        );
    }
    Ok(())
}

fn required_env(name: &str) -> BuildResult<String> {
    env::var(name).map_err(|_| format!("Cargo did not provide {name}").into())
}

fn resolve_user_path(value: OsString) -> BuildResult<PathBuf> {
    let path = PathBuf::from(value);
    if path.is_absolute() {
        return Ok(path);
    }
    Ok(PathBuf::from(required_env("CARGO_MANIFEST_DIR")?).join(path))
}

fn expected_archive_hash(
    local_archive: Option<&Path>,
    release: Option<&ReleaseArtifact>,
) -> BuildResult<String> {
    if let Some(value) = env::var_os(ENV_NATIVE_SHA256) {
        return normalize_sha256(&value.to_string_lossy());
    }
    if let Some(artifact) = release {
        return normalize_sha256(&artifact.sha256);
    }
    if let Some(archive) = local_archive {
        let checksum_path = appended_extension(archive, ".sha256");
        println!("cargo::rerun-if-changed={}", checksum_path.display());
        let checksum = fs::read_to_string(&checksum_path).map_err(|error| {
            format!(
                "no embedded checksum is available; failed to read {}: {error}",
                checksum_path.display()
            )
        })?;
        let first_field = checksum
            .split_whitespace()
            .next()
            .ok_or_else(|| format!("{} is empty", checksum_path.display()))?;
        return normalize_sha256(first_field);
    }
    Err("no native archive checksum is available".into())
}

fn appended_extension(path: &Path, suffix: &str) -> PathBuf {
    let mut value = path.as_os_str().to_os_string();
    value.push(suffix);
    PathBuf::from(value)
}

fn normalize_sha256(value: &str) -> BuildResult<String> {
    let value = value.trim().to_ascii_lowercase();
    if value.len() != 64 || !value.bytes().all(|byte| byte.is_ascii_hexdigit()) {
        return Err(format!("invalid SHA-256 value {value:?}").into());
    }
    Ok(value)
}

fn cache_root() -> BuildResult<PathBuf> {
    if let Some(path) = env::var_os(ENV_CACHE_DIR) {
        return resolve_user_path(path);
    }
    if let Some(path) = env::var_os("CARGO_HOME") {
        return Ok(PathBuf::from(path).join("slang-slim/native"));
    }
    if let Some(path) = env::var_os("USERPROFILE") {
        return Ok(PathBuf::from(path).join(".cargo/slang-slim/native"));
    }
    if let Some(path) = env::var_os("HOME") {
        return Ok(PathBuf::from(path).join(".cargo/slang-slim/native"));
    }
    Ok(PathBuf::from(required_env("OUT_DIR")?).join("slang-slim-cache"))
}

#[allow(clippy::too_many_arguments)]
fn ensure_cached_archive(
    cache_root: &Path,
    archive_name: &str,
    expected_hash: &str,
    local_archive: Option<&Path>,
    release: Option<&ReleaseArtifact>,
    indexed_base_url: Option<&str>,
    version: &str,
) -> BuildResult<PathBuf> {
    validate_single_line("archive name", archive_name)?;
    if Path::new(archive_name)
        .file_name()
        .and_then(|name| name.to_str())
        != Some(archive_name)
    {
        return Err(format!("archive name must be a plain file name: {archive_name}").into());
    }

    let download_directory = cache_root.join("downloads").join(expected_hash);
    fs::create_dir_all(&download_directory)?;
    let cached_archive = download_directory.join(archive_name);
    if cached_archive.is_file() {
        if sha256_file(&cached_archive)? == expected_hash {
            return Ok(cached_archive);
        }
        fs::remove_file(&cached_archive)?;
    }

    let temporary = unique_temporary_path(&download_directory, "archive.part")?;
    let result: BuildResult<()> = if let Some(source) = local_archive {
        let actual_hash = sha256_file(source)?;
        if actual_hash != expected_hash {
            Err(format!(
                "native archive {} has SHA-256 {actual_hash}, expected {expected_hash}",
                source.display()
            )
            .into())
        } else {
            fs::copy(source, &temporary)?;
            Ok(())
        }
    } else {
        let artifact = release.ok_or_else(|| {
            format!(
                "no release metadata for version {version}; set {ENV_NATIVE_ARCHIVE} or \
                 {ENV_NATIVE_DIR} for a local build"
            )
        })?;
        let base_url = env::var(ENV_RELEASE_BASE_URL)
            .ok()
            .or_else(|| indexed_base_url.map(str::to_owned))
            .filter(|value| !value.trim().is_empty())
            .ok_or_else(|| {
                format!(
                    "no release base URL is configured; set {ENV_RELEASE_BASE_URL} or publish \
                     it in native-artifacts.json"
                )
            })?;
        if downloads_disabled() {
            return Err(format!(
                "native archive is not cached and downloads are disabled; provide {ENV_NATIVE_ARCHIVE}"
            )
            .into());
        }
        let url = format!(
            "{}/v{version}/{}",
            base_url.trim_end_matches('/'),
            artifact.archive
        );
        download(&url, &temporary)?;
        let actual_hash = sha256_file(&temporary)?;
        if actual_hash != expected_hash {
            Err(
                format!("downloaded {url} with SHA-256 {actual_hash}, expected {expected_hash}")
                    .into(),
            )
        } else {
            Ok(())
        }
    };

    if let Err(error) = result {
        let _ = fs::remove_file(&temporary);
        return Err(error);
    }
    fs::rename(&temporary, &cached_archive)?;
    Ok(cached_archive)
}

fn downloads_disabled() -> bool {
    env_truthy(ENV_DISABLE_DOWNLOAD) || env_truthy("CARGO_NET_OFFLINE")
}

fn env_truthy(name: &str) -> bool {
    env::var(name)
        .map(|value| {
            matches!(
                value.to_ascii_lowercase().as_str(),
                "1" | "true" | "yes" | "on"
            )
        })
        .unwrap_or(false)
}

fn download(url: &str, destination: &Path) -> BuildResult<()> {
    println!("cargo::warning=downloading slang-slim native asset from {url}");
    let mut response = ureq::get(url).call()?;
    let mut reader = response.body_mut().as_reader();
    let mut output = File::create(destination)?;
    io::copy(&mut reader, &mut output)?;
    output.flush()?;
    Ok(())
}

fn ensure_extracted(
    cache_root: &Path,
    archive_path: &Path,
    archive_hash: &str,
    target: &str,
    version: &str,
) -> BuildResult<PathBuf> {
    let target_root = cache_root.join("artifacts").join(target);
    fs::create_dir_all(&target_root)?;
    let extracted_root = target_root.join(archive_hash);
    if extracted_root.is_dir() {
        if validate_native_root(&extracted_root, target, version).is_ok() {
            return Ok(extracted_root);
        }
        remove_cache_directory(&target_root, &extracted_root)?;
    }

    let temporary = unique_temporary_path(&target_root, "extract.part")?;
    fs::create_dir(&temporary)?;
    let extraction_result = (|| -> BuildResult<()> {
        extract_zip(archive_path, &temporary)?;
        validate_native_root(&temporary, target, version)?;
        fs::rename(&temporary, &extracted_root)?;
        Ok(())
    })();
    if extraction_result.is_err() && temporary.exists() {
        let _ = remove_cache_directory(&target_root, &temporary);
    }
    extraction_result?;
    Ok(extracted_root)
}

fn extract_zip(archive_path: &Path, destination: &Path) -> BuildResult<()> {
    let file = File::open(archive_path)?;
    let mut archive = zip::ZipArchive::new(BufReader::new(file))?;
    for index in 0..archive.len() {
        let mut entry = archive.by_index(index)?;
        let relative = entry
            .enclosed_name()
            .ok_or_else(|| format!("ZIP entry {:?} escapes the destination", entry.name()))?;
        let output_path = destination.join(relative);
        if entry.is_dir() {
            fs::create_dir_all(&output_path)?;
        } else if entry.is_file() {
            if let Some(parent) = output_path.parent() {
                fs::create_dir_all(parent)?;
            }
            let mut output = File::create(output_path)?;
            io::copy(&mut entry, &mut output)?;
        } else {
            return Err(format!("ZIP entry {:?} is not a regular file", entry.name()).into());
        }
    }
    Ok(())
}

fn unique_temporary_path(parent: &Path, stem: &str) -> BuildResult<PathBuf> {
    for counter in 0..1000_u32 {
        let candidate = parent.join(format!(".{stem}-{}-{counter}", std::process::id()));
        if !candidate.exists() {
            return Ok(candidate);
        }
    }
    Err(format!(
        "could not allocate a temporary path below {}",
        parent.display()
    )
    .into())
}

fn remove_cache_directory(parent: &Path, path: &Path) -> BuildResult<()> {
    let parent = parent.canonicalize()?;
    let path = path.canonicalize()?;
    if path == parent || !path.starts_with(&parent) {
        return Err(format!("refusing to remove cache path {}", path.display()).into());
    }
    fs::remove_dir_all(path)?;
    Ok(())
}

fn validate_native_root(
    native_root: &Path,
    expected_target: &str,
    expected_version: &str,
) -> BuildResult<NativeManifest> {
    let manifest_path = native_root.join("manifest.json");
    let manifest: NativeManifest =
        serde_json::from_reader(BufReader::new(File::open(&manifest_path)?))?;
    if manifest.schema_version != MANIFEST_SCHEMA_VERSION
        || manifest.package != "slang-slim-native"
        || manifest.abi_version != ABI_VERSION
        || manifest.target != expected_target
        || manifest.version != expected_version
    {
        return Err(format!(
            "{} does not match package slang-slim-native ABI {ABI_VERSION}, version \
             {expected_version}, target {expected_target}",
            manifest_path.display()
        )
        .into());
    }
    if manifest.link.kind != "static" {
        return Err(format!("unsupported native link kind {}", manifest.link.kind).into());
    }

    let search_relative = safe_relative_path(&manifest.link.search_path)?;
    let mut recorded_paths = HashSet::new();
    for file in &manifest.files {
        let relative = safe_relative_path(&file.path)?;
        if !recorded_paths.insert(relative.clone()) {
            return Err(format!("duplicate manifest file {}", file.path).into());
        }
        let path = native_root.join(relative);
        let metadata = fs::metadata(&path)?;
        if !metadata.is_file() || metadata.len() != file.size {
            return Err(format!("native payload size mismatch for {}", path.display()).into());
        }
        let expected_hash = normalize_sha256(&file.sha256)?;
        let actual_hash = sha256_file(&path)?;
        if actual_hash != expected_hash {
            return Err(format!(
                "native payload {} has SHA-256 {actual_hash}, expected {expected_hash}",
                path.display()
            )
            .into());
        }
    }

    for library in &manifest.link.libraries {
        validate_link_name(&library.name)?;
        let relative = safe_relative_path(&library.path)?;
        if !relative.starts_with(&search_relative)
            || !recorded_paths.contains(&relative)
            || !native_root.join(relative).is_file()
        {
            return Err(format!(
                "native library {} is missing from the payload",
                library.path
            )
            .into());
        }
    }
    for library in &manifest.link.runtime_libraries {
        validate_link_name(library)?;
    }
    for library in &manifest.link.system_libraries {
        validate_link_name(library)?;
    }
    for argument in &manifest.link.arguments {
        validate_single_line("link argument", argument)?;
    }
    Ok(manifest)
}

fn safe_relative_path(value: &str) -> BuildResult<PathBuf> {
    validate_single_line("relative path", value)?;
    let path = PathBuf::from(value);
    if path.as_os_str().is_empty()
        || path.is_absolute()
        || path.components().any(|component| {
            matches!(
                component,
                Component::ParentDir | Component::RootDir | Component::Prefix(_)
            )
        })
    {
        return Err(format!("unsafe relative path {value:?}").into());
    }
    Ok(path)
}

fn validate_link_name(value: &str) -> BuildResult<()> {
    validate_single_line("link library", value)?;
    if value.is_empty()
        || !value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'_' | b'-' | b'+' | b'.'))
    {
        return Err(format!("invalid link library name {value:?}").into());
    }
    Ok(())
}

fn validate_single_line(label: &str, value: &str) -> BuildResult<()> {
    if value.contains('\r') || value.contains('\n') {
        return Err(format!("{label} must be a single line").into());
    }
    Ok(())
}

fn emit_link_instructions(native_root: &Path, manifest: &NativeManifest) -> BuildResult<()> {
    let search_path = native_root.join(safe_relative_path(&manifest.link.search_path)?);
    if !search_path.is_dir() {
        return Err(format!("native link directory {} is missing", search_path.display()).into());
    }
    println!("cargo::rustc-link-search=native={}", search_path.display());
    for library in &manifest.link.libraries {
        println!("cargo::rustc-link-lib=static={}", library.name);
    }
    for library in &manifest.link.runtime_libraries {
        println!("cargo::rustc-link-lib=static={library}");
    }
    for library in &manifest.link.system_libraries {
        println!("cargo::rustc-link-lib={library}");
    }
    for argument in &manifest.link.arguments {
        println!("cargo::rustc-link-arg={argument}");
    }
    println!("cargo::metadata=native_root={}", native_root.display());
    println!("cargo::rustc-cfg=slang_slim_native_linked");
    Ok(())
}

fn sha256_file(path: &Path) -> BuildResult<String> {
    let mut file = BufReader::new(File::open(path)?);
    let mut digest = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let count = file.read(&mut buffer)?;
        if count == 0 {
            break;
        }
        digest.update(&buffer[..count]);
    }
    Ok(format!("{:x}", digest.finalize()))
}
