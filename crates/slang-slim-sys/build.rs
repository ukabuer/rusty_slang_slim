use fs2::FileExt;
use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::collections::HashSet;
use std::env;
use std::error::Error;
use std::ffi::{OsStr, OsString};
use std::fs::{self, File, OpenOptions};
use std::io::{self, BufReader, Read, Write};
use std::path::{Component, Path, PathBuf};
use std::process::Command;

const MANIFEST_SCHEMA_VERSION: u32 = 1;
const ABI_VERSION: u32 = 1;
const SUPPORTED_TARGETS: [&str; 2] = ["x86_64-pc-windows-msvc", "aarch64-linux-android"];
const MERGED_LIBRARY_NAME: &str = "slang-slim";
const DEFAULT_RELEASE_BASE_URL: &str =
    "https://github.com/ukabuer/rusty_slang_slim/releases/download";
const DEFAULT_GITHUB_API_BASE_URL: &str = "https://api.github.com";
const DEFAULT_GITHUB_REPOSITORY: &str = "ukabuer/rusty_slang_slim";

const ENV_NATIVE_DIR: &str = "SLANG_SLIM_NATIVE_DIR";
const ENV_NATIVE_ARCHIVE: &str = "SLANG_SLIM_NATIVE_ARCHIVE";
const ENV_FROM_SOURCE: &str = "SLANG_SLIM_FROM_SOURCE";
const ENV_NATIVE_SHA256: &str = "SLANG_SLIM_NATIVE_SHA256";
const ENV_CACHE_DIR: &str = "SLANG_SLIM_CACHE_DIR";
const ENV_RELEASE_BASE_URL: &str = "SLANG_SLIM_RELEASE_BASE_URL";
const ENV_DISABLE_DOWNLOAD: &str = "SLANG_SLIM_DISABLE_DOWNLOAD";
const ENV_ANDROID_NDK_HOME: &str = "ANDROID_NDK_HOME";
const ENV_ANDROID_NDK_ROOT: &str = "ANDROID_NDK_ROOT";
const NATIVE_ARCHIVE_PREFIX: &str = "slang-slim-native-v";

type BuildResult<T> = Result<T, Box<dyn Error + Send + Sync>>;

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

#[derive(Debug, Deserialize)]
struct GitHubRelease {
    assets: Vec<GitHubReleaseAsset>,
}

#[derive(Debug, Deserialize)]
struct GitHubReleaseAsset {
    name: String,
    digest: Option<String>,
}

#[derive(Clone, Copy)]
struct LocalLibrary {
    name: &'static str,
    relative_path: &'static str,
}

struct LocalNativeLayout {
    libraries: &'static [LocalLibrary],
    runtime_libraries: &'static [&'static str],
    system_libraries: &'static [&'static str],
    arguments: &'static [&'static str],
}

const WINDOWS_LOCAL_LIBRARIES: &[LocalLibrary] = &[
    LocalLibrary {
        name: "slang-slim-c-api",
        relative_path: "Release/slang-slim-c-api.lib",
    },
    LocalLibrary {
        name: "slang-compiler",
        relative_path: "slang/Release/lib/slang-compiler.lib",
    },
    LocalLibrary {
        name: "compiler-core",
        relative_path: "slang/Release/lib/compiler-core.lib",
    },
    LocalLibrary {
        name: "core",
        relative_path: "slang/Release/lib/core.lib",
    },
    LocalLibrary {
        name: "miniz",
        relative_path: "slang/external/miniz/Release/miniz.lib",
    },
    LocalLibrary {
        name: "lz4",
        relative_path: "slang/external/lz4/build/cmake/Release/lz4.lib",
    },
    LocalLibrary {
        name: "cmark-gfm",
        relative_path: "slang/external/cmark/src/Release/cmark-gfm.lib",
    },
];

const ANDROID_LOCAL_LIBRARIES: &[LocalLibrary] = &[
    LocalLibrary {
        name: "slang-slim-c-api",
        relative_path: "Release/libslang-slim-c-api.a",
    },
    LocalLibrary {
        name: "slang-compiler",
        relative_path: "slang/Release/lib/libslang-compiler.a",
    },
    LocalLibrary {
        name: "compiler-core",
        relative_path: "slang/Release/lib/libcompiler-core.a",
    },
    LocalLibrary {
        name: "core",
        relative_path: "slang/Release/lib/libcore.a",
    },
    LocalLibrary {
        name: "miniz",
        relative_path: "slang/external/miniz/Release/libminiz.a",
    },
    LocalLibrary {
        name: "lz4",
        relative_path: "slang/external/lz4/build/cmake/Release/liblz4.a",
    },
    LocalLibrary {
        name: "cmark-gfm",
        relative_path: "slang/external/cmark/src/Release/libcmark-gfm.a",
    },
];

const WINDOWS_RUNTIME_LIBRARIES: &[&str] = &[];
const WINDOWS_SYSTEM_LIBRARIES: &[&str] = &[
    "kernel32", "user32", "gdi32", "winspool", "shell32", "ole32", "oleaut32", "uuid", "comdlg32",
    "advapi32",
];
const WINDOWS_LINK_ARGUMENTS: &[&str] = &[];

const ANDROID_RUNTIME_LIBRARIES: &[&str] = &["c++_static", "c++abi"];
const ANDROID_SYSTEM_LIBRARIES: &[&str] = &["dl", "atomic", "m"];
const ANDROID_LINK_ARGUMENTS: &[&str] = &["-pthread"];

fn main() {
    println!("cargo::rerun-if-changed=build.rs");
    println!("cargo::rerun-if-changed=../../native/include/slang_c_api.h");
    println!("cargo::rustc-check-cfg=cfg(slang_slim_native_linked)");
    for variable in [
        ENV_NATIVE_DIR,
        ENV_NATIVE_ARCHIVE,
        ENV_FROM_SOURCE,
        ENV_NATIVE_SHA256,
        ENV_CACHE_DIR,
        ENV_RELEASE_BASE_URL,
        ENV_DISABLE_DOWNLOAD,
        ENV_ANDROID_NDK_HOME,
        ENV_ANDROID_NDK_ROOT,
        "CARGO_NET_OFFLINE",
        "CARGO_HOME",
        "CARGO_CFG_TARGET_FEATURE",
        "CARGO_FEATURE_NATIVE",
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
    let archive_name = native_archive_name(&version, &target);
    let native_dir = env::var_os(ENV_NATIVE_DIR);
    let native_archive = env::var_os(ENV_NATIVE_ARCHIVE);
    let from_source = env_truthy(ENV_FROM_SOURCE);
    if from_source {
        println!("cargo::rerun-if-changed=../../native");
        println!("cargo::rerun-if-changed=../../third_party/slang");
        if native_dir.is_some() || native_archive.is_some() {
            println!(
                "cargo::warning={ENV_FROM_SOURCE}=1 takes precedence over native archive/directory overrides"
            );
        }
    } else if native_dir.is_some() && native_archive.is_some() {
        return Err(format!("set only one of {ENV_NATIVE_DIR} and {ENV_NATIVE_ARCHIVE}").into());
    }

    let native_required = env::var_os("CARGO_FEATURE_NATIVE").is_some();
    if !native_required && native_dir.is_none() && native_archive.is_none() && !from_source {
        println!(
            "cargo::warning=slang-slim-sys built without feature `native`; native linking is skipped. Set {ENV_NATIVE_ARCHIVE}, {ENV_NATIVE_DIR}, or {ENV_FROM_SOURCE}=1 to exercise native linking"
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

    if from_source {
        let repository_root = repository_root()?;
        let _lock = acquire_source_build_lock(&repository_root)?;
        let build_root = build_native_from_source(&repository_root, &target)?;
        emit_local_build_link_instructions(&build_root, &target)?;
        return Ok(());
    }

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
        let release_base_url = release_base_url();
        let expected_hash = explicit_archive_hash(archive_source.as_deref())?;
        let archive_url = if archive_source.is_none() {
            Some(native_archive_url(&release_base_url, &version, &target))
        } else {
            None
        };
        let release_api_url = if archive_source.is_none() {
            github_release_api_url(&release_base_url, &version)
        } else {
            None
        };
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

        let (cached_archive, archive_hash) = ensure_cached_archive(
            &cache_root,
            &archive_name,
            expected_hash.as_deref(),
            archive_source.as_deref(),
            archive_url.as_deref(),
            release_api_url.as_deref(),
        )?;
        ensure_extracted(
            &cache_root,
            &cached_archive,
            &archive_hash,
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

fn explicit_archive_hash(local_archive: Option<&Path>) -> BuildResult<Option<String>> {
    if let Some(value) = env::var_os(ENV_NATIVE_SHA256) {
        return Ok(Some(normalize_sha256(&value.to_string_lossy())?));
    }
    if let Some(archive) = local_archive {
        return Ok(Some(sha256_file(archive)?));
    }
    Ok(None)
}

fn native_archive_name(version: &str, target: &str) -> String {
    format!("{NATIVE_ARCHIVE_PREFIX}{version}-{target}.zip")
}

fn native_archive_url(base_url: &str, version: &str, target: &str) -> String {
    format!(
        "{}/v{version}/{}",
        base_url.trim_end_matches('/'),
        native_archive_name(version, target)
    )
}

fn github_release_api_url(base_url: &str, version: &str) -> Option<String> {
    if base_url.trim_end_matches('/') != DEFAULT_RELEASE_BASE_URL {
        return None;
    }
    Some(format!(
        "{DEFAULT_GITHUB_API_BASE_URL}/repos/{DEFAULT_GITHUB_REPOSITORY}/releases/tags/v{version}"
    ))
}

fn release_base_url() -> String {
    env::var(ENV_RELEASE_BASE_URL)
        .ok()
        .filter(|value| !value.trim().is_empty())
        .unwrap_or_else(|| DEFAULT_RELEASE_BASE_URL.to_owned())
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

fn ensure_cached_archive(
    cache_root: &Path,
    archive_name: &str,
    expected_hash: Option<&str>,
    local_archive: Option<&Path>,
    remote_url: Option<&str>,
    remote_api_url: Option<&str>,
) -> BuildResult<(PathBuf, String)> {
    validate_single_line("archive name", archive_name)?;
    if Path::new(archive_name)
        .file_name()
        .and_then(|name| name.to_str())
        != Some(archive_name)
    {
        return Err(format!("archive name must be a plain file name: {archive_name}").into());
    }

    let expected_hash = if let Some(value) = expected_hash {
        normalize_sha256(value)?
    } else if let Some(api_url) = remote_api_url {
        remote_asset_digest(cache_root, api_url, archive_name)?
    } else {
        return Err(format!(
            "no checksum source is available for the native archive; set {ENV_NATIVE_SHA256} when using a custom release mirror"
        )
        .into());
    };
    let download_directory = cache_root.join("downloads").join(&expected_hash);
    fs::create_dir_all(&download_directory)?;
    let cached_archive = download_directory.join(archive_name);
    if cached_archive.is_file() {
        if sha256_file(&cached_archive)? == expected_hash {
            return Ok((cached_archive, expected_hash));
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
        let url = remote_url.ok_or_else(|| {
            format!("a remote archive URL is required when {ENV_NATIVE_ARCHIVE} is not set")
        })?;
        if downloads_disabled() {
            return Err(format!(
                "native archive is not cached and downloads are disabled; provide {ENV_NATIVE_ARCHIVE}"
            )
            .into());
        }
        download(url, &temporary)?;
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
    Ok((cached_archive, expected_hash))
}

fn remote_asset_digest(
    cache_root: &Path,
    api_url: &str,
    archive_name: &str,
) -> BuildResult<String> {
    let digest_directory = cache_root.join("digests");
    fs::create_dir_all(&digest_directory)?;
    let cache_key = format!("{api_url}\n{archive_name}");
    let digest_path =
        digest_directory.join(format!("{}.digest", sha256_bytes(cache_key.as_bytes())));
    if digest_path.is_file() {
        match normalize_github_digest(&fs::read_to_string(&digest_path)?) {
            Ok(hash) => return Ok(hash),
            Err(error) if downloads_disabled() => {
                return Err(format!(
                    "cached native release digest {} is invalid and downloads are disabled: {error}",
                    digest_path.display()
                )
                .into());
            }
            Err(_) => fs::remove_file(&digest_path)?,
        }
    }
    if downloads_disabled() {
        return Err(format!(
            "native release asset digest is not cached and downloads are disabled; provide {ENV_NATIVE_SHA256} or {ENV_NATIVE_ARCHIVE}"
        )
        .into());
    }

    println!("cargo::warning=querying native asset digest from {api_url}");
    let mut response = ureq::get(api_url)
        .header("Accept", "application/vnd.github+json")
        .header("X-GitHub-Api-Version", "2022-11-28")
        .header("User-Agent", "slang-slim-sys")
        .call()?;
    let mut body = String::new();
    response.body_mut().as_reader().read_to_string(&mut body)?;
    let release: GitHubRelease = serde_json::from_str(&body)?;
    let asset = release
        .assets
        .iter()
        .find(|asset| asset.name == archive_name)
        .ok_or_else(|| format!("GitHub Release API response has no asset named {archive_name}"))?;
    let digest = asset.digest.as_deref().ok_or_else(|| {
        format!(
            "GitHub Release asset {archive_name} has no SHA-256 digest; set {ENV_NATIVE_SHA256}"
        )
    })?;
    let hash = normalize_github_digest(digest)?;
    let temporary = unique_temporary_path(&digest_directory, "digest.part")?;
    let result: BuildResult<String> = (|| {
        let mut output = File::create(&temporary)?;
        output.write_all(digest.as_bytes())?;
        output.flush()?;
        fs::rename(&temporary, &digest_path)?;
        Ok(hash)
    })();
    if result.is_err() && temporary.exists() {
        let _ = fs::remove_file(&temporary);
    }
    result
}

fn normalize_github_digest(value: &str) -> BuildResult<String> {
    let (algorithm, digest) = value
        .trim()
        .split_once(':')
        .ok_or_else(|| format!("invalid GitHub asset digest {value:?}"))?;
    if !algorithm.eq_ignore_ascii_case("sha256") {
        return Err(format!("unsupported GitHub asset digest algorithm {algorithm:?}").into());
    }
    normalize_sha256(digest)
}

fn sha256_bytes(value: &[u8]) -> String {
    let mut digest = Sha256::new();
    digest.update(value);
    format!("{:x}", digest.finalize())
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

    let expected_library_path = merged_library_path(expected_target)?;
    if manifest.link.libraries.len() != 1 {
        return Err(format!(
            "native package must expose exactly one merged library named {MERGED_LIBRARY_NAME}; found {}",
            manifest.link.libraries.len()
        )
        .into());
    }
    let merged_library = &manifest.link.libraries[0];
    if merged_library.name != MERGED_LIBRARY_NAME || merged_library.path != expected_library_path {
        return Err(format!(
            "native package must expose {MERGED_LIBRARY_NAME} at {expected_library_path}; found {} at {}",
            merged_library.name, merged_library.path
        )
        .into());
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

fn merged_library_path(target: &str) -> BuildResult<&'static str> {
    match target {
        "x86_64-pc-windows-msvc" => Ok("lib/slang-slim.lib"),
        "aarch64-linux-android" => Ok("lib/libslang-slim.a"),
        _ => Err(format!("unsupported Rust target {target} for merged native library").into()),
    }
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

fn repository_root() -> BuildResult<PathBuf> {
    let manifest_dir = PathBuf::from(required_env("CARGO_MANIFEST_DIR")?);
    manifest_dir
        .parent()
        .and_then(Path::parent)
        .map(Path::to_path_buf)
        .ok_or_else(|| "could not determine the repository root".into())
}

fn acquire_source_build_lock(repository_root: &Path) -> BuildResult<File> {
    let build_directory = repository_root.join("build");
    fs::create_dir_all(&build_directory)?;
    let lock_path = build_directory.join("slang-slim-source.lock");
    let lock = OpenOptions::new()
        .create(true)
        .truncate(false)
        .read(true)
        .write(true)
        .open(lock_path)?;
    FileExt::lock_exclusive(&lock)?;
    Ok(lock)
}

fn run_cmake<I, S>(
    source_directory: &Path,
    arguments: I,
    environment: &[(&str, &OsStr)],
    description: &str,
) -> BuildResult<()>
where
    I: IntoIterator<Item = S>,
    S: AsRef<OsStr>,
{
    let mut command = Command::new("cmake");
    command.current_dir(source_directory);
    for argument in arguments {
        command.arg(argument);
    }
    for (name, value) in environment {
        command.env(name, value);
    }
    let status = command
        .status()
        .map_err(|error| format!("{description}: failed to start CMake: {error}"))?;
    if !status.success() {
        return Err(format!("{description} failed with {status}").into());
    }
    Ok(())
}

fn ensure_cmake_configured(
    source_directory: &Path,
    build_directory: &Path,
    preset: &str,
    environment: &[(&str, &OsStr)],
) -> BuildResult<()> {
    if build_directory.join("CMakeCache.txt").is_file() {
        return Ok(());
    }
    run_cmake(
        source_directory,
        ["--preset", preset],
        environment,
        &format!("configure CMake preset {preset}"),
    )
}

fn android_ndk_home(repository_root: &Path) -> BuildResult<OsString> {
    for variable in [ENV_ANDROID_NDK_HOME, ENV_ANDROID_NDK_ROOT] {
        if let Some(value) = env::var_os(variable) {
            if Path::new(&value).is_dir() {
                return Ok(value);
            }
            return Err(format!(
                "{variable} does not point to a directory: {}",
                Path::new(&value).display()
            )
            .into());
        }
    }
    let bundled = repository_root.join("build/toolchains/android-ndk-r27d");
    if bundled.is_dir() {
        return Ok(bundled.into_os_string());
    }
    Err(format!(
        "Android source builds require {ENV_ANDROID_NDK_HOME} or {ENV_ANDROID_NDK_ROOT}, or build/toolchains/android-ndk-r27d"
    )
    .into())
}

fn ensure_android_host_tools(
    repository_root: &Path,
    source_directory: &Path,
    environment: &[(&str, &OsStr)],
) -> BuildResult<()> {
    let host_tools = repository_root.join("build/native/host-tools");
    let generator = host_tools.join("bin/slang-generate.exe");
    if generator.is_file() {
        return Ok(());
    }

    let windows_build = repository_root.join("build/native/windows-x64");
    ensure_cmake_configured(source_directory, &windows_build, "windows-x64", &[])?;
    run_cmake(
        source_directory,
        [
            "--build",
            "--preset",
            "windows-x64-generators",
            "--parallel",
        ],
        &[],
        "build Slang host generators",
    )?;

    let arguments = vec![
        OsString::from("--install"),
        windows_build.as_os_str().to_os_string(),
        OsString::from("--config"),
        OsString::from("Release"),
        OsString::from("--prefix"),
        host_tools.as_os_str().to_os_string(),
        OsString::from("--component"),
        OsString::from("generators"),
    ];
    run_cmake(
        source_directory,
        arguments,
        environment,
        "install Slang host generators",
    )
}

fn build_native_from_source(repository_root: &Path, target: &str) -> BuildResult<PathBuf> {
    let source_directory = repository_root.join("native");
    if !source_directory.join("CMakePresets.json").is_file() {
        return Err(format!(
            "native CMake source tree is missing {}",
            source_directory.display()
        )
        .into());
    }

    match target {
        "x86_64-pc-windows-msvc" => {
            let build_directory = repository_root.join("build/native/windows-x64");
            ensure_cmake_configured(&source_directory, &build_directory, "windows-x64", &[])?;
            run_cmake(
                &source_directory,
                ["--build", "--preset", "windows-x64-release", "--parallel"],
                &[],
                "build Windows native Slang",
            )?;
            Ok(build_directory)
        }
        "aarch64-linux-android" => {
            let ndk_home = android_ndk_home(repository_root)?;
            let environment = [(ENV_ANDROID_NDK_HOME, ndk_home.as_os_str())];
            ensure_android_host_tools(repository_root, &source_directory, &environment)?;

            let build_directory = repository_root.join("build/native/android-arm64");
            ensure_cmake_configured(
                &source_directory,
                &build_directory,
                "android-arm64",
                &environment,
            )?;
            run_cmake(
                &source_directory,
                ["--build", "--preset", "android-arm64-release", "--parallel"],
                &environment,
                "build Android native Slang",
            )?;
            Ok(build_directory)
        }
        _ => Err(format!("unsupported Rust target {target} for source build").into()),
    }
}

fn local_native_layout(target: &str) -> Option<LocalNativeLayout> {
    match target {
        "x86_64-pc-windows-msvc" => Some(LocalNativeLayout {
            libraries: WINDOWS_LOCAL_LIBRARIES,
            runtime_libraries: WINDOWS_RUNTIME_LIBRARIES,
            system_libraries: WINDOWS_SYSTEM_LIBRARIES,
            arguments: WINDOWS_LINK_ARGUMENTS,
        }),
        "aarch64-linux-android" => Some(LocalNativeLayout {
            libraries: ANDROID_LOCAL_LIBRARIES,
            runtime_libraries: ANDROID_RUNTIME_LIBRARIES,
            system_libraries: ANDROID_SYSTEM_LIBRARIES,
            arguments: ANDROID_LINK_ARGUMENTS,
        }),
        _ => None,
    }
}

fn emit_local_build_link_instructions(native_build_root: &Path, target: &str) -> BuildResult<()> {
    if !native_build_root.is_dir() {
        return Err(format!(
            "local native build directory {} does not exist",
            native_build_root.display()
        )
        .into());
    }
    let layout = local_native_layout(target)
        .ok_or_else(|| format!("unsupported Rust target {target} for local native build"))?;

    let mut search_paths = Vec::new();
    for library in layout.libraries {
        let relative = safe_relative_path(library.relative_path)?;
        let path = native_build_root.join(&relative);
        if !path.is_file() {
            return Err(format!(
                "local native library {} is missing; build the Release target before running Cargo",
                path.display()
            )
            .into());
        }
        println!("cargo::rerun-if-changed={}", path.display());
        let parent = path
            .parent()
            .ok_or_else(|| format!("local native library {} has no parent", path.display()))?;
        if !search_paths.iter().any(|candidate| candidate == parent) {
            search_paths.push(parent.to_owned());
        }
    }

    for search_path in search_paths {
        println!("cargo::rustc-link-search=native={}", search_path.display());
    }
    emit_runtime_link_search_path(
        target,
        layout
            .runtime_libraries
            .iter()
            .any(|library| matches!(*library, "c++_static" | "c++abi")),
    )?;
    for library in layout.libraries {
        println!("cargo::rustc-link-lib=static={}", library.name);
    }
    for library in layout.runtime_libraries {
        println!("cargo::rustc-link-lib=static={library}");
    }
    for library in layout.system_libraries {
        println!("cargo::rustc-link-lib={library}");
    }
    for argument in layout.arguments {
        println!("cargo::rustc-link-arg={argument}");
    }
    println!(
        "cargo::warning=using local native CMake build at {}; downloads are skipped",
        native_build_root.display()
    );
    println!(
        "cargo::metadata=native_root={}",
        native_build_root.display()
    );
    println!("cargo::rustc-cfg=slang_slim_native_linked");
    Ok(())
}

fn emit_link_instructions(native_root: &Path, manifest: &NativeManifest) -> BuildResult<()> {
    let search_path = native_root.join(safe_relative_path(&manifest.link.search_path)?);
    if !search_path.is_dir() {
        return Err(format!("native link directory {} is missing", search_path.display()).into());
    }
    println!("cargo::rustc-link-search=native={}", search_path.display());
    emit_runtime_link_search_path(
        &manifest.target,
        manifest
            .link
            .runtime_libraries
            .iter()
            .any(|library| matches!(library.as_str(), "c++_static" | "c++abi")),
    )?;
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

fn emit_runtime_link_search_path(target: &str, needs_android_cxx_runtime: bool) -> BuildResult<()> {
    if target != "aarch64-linux-android" || !needs_android_cxx_runtime {
        return Ok(());
    }
    let library_directory = android_ndk_cxx_library_directory()?;
    println!(
        "cargo::rustc-link-search=native={}",
        library_directory.display()
    );
    Ok(())
}

fn android_ndk_cxx_library_directory() -> BuildResult<PathBuf> {
    let mut configured_roots: Vec<PathBuf> = [ENV_ANDROID_NDK_HOME, ENV_ANDROID_NDK_ROOT]
        .into_iter()
        .filter_map(|name| env::var_os(name).map(PathBuf::from))
        .collect();
    if configured_roots.is_empty() {
        let bundled = repository_root()?.join("build/toolchains/android-ndk-r27d");
        if bundled.is_dir() {
            configured_roots.push(bundled);
        }
    }
    if configured_roots.is_empty() {
        return Err(format!(
            "Android native linking requires {ENV_ANDROID_NDK_HOME} or {ENV_ANDROID_NDK_ROOT} (or build/toolchains/android-ndk-r27d for a source build) to locate libc++_static.a and libc++abi.a"
        )
        .into());
    }

    for ndk_root in configured_roots {
        if !ndk_root.is_dir() {
            continue;
        }
        let prebuilt_root = ndk_root.join("toolchains/llvm/prebuilt");
        let mut hosts: Vec<PathBuf> = fs::read_dir(&prebuilt_root)
            .map_err(|error| {
                format!(
                    "failed to inspect Android NDK prebuilt directory {}: {error}",
                    prebuilt_root.display()
                )
            })?
            .filter_map(|entry| entry.ok().map(|entry| entry.path()))
            .filter(|path| path.is_dir())
            .collect();
        hosts.sort();
        for host in hosts {
            let library_directory = host.join("sysroot/usr/lib/aarch64-linux-android");
            if library_directory.join("libc++_static.a").is_file()
                && library_directory.join("libc++abi.a").is_file()
            {
                return Ok(library_directory);
            }
        }
    }

    Err(format!(
        "could not locate libc++_static.a and libc++abi.a below {ENV_ANDROID_NDK_HOME} or {ENV_ANDROID_NDK_ROOT}; install an Android NDK and set one of those variables"
    )
    .into())
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
