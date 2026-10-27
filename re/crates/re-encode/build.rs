use std::path::PathBuf;

fn main() {
    assert_eq!(
        std::env::var("CARGO_CFG_TARGET_OS").unwrap(),
        "windows",
        "re-encode requires Windows"
    );
    assert_eq!(
        std::env::var("CARGO_CFG_TARGET_ARCH").unwrap(),
        "x86_64",
        "re-encode requires x64"
    );
    let root = PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let vpl = root.join("../../../endpoint/third_party/libvpl");
    let mut dispatcher = cc::Build::new();
    dispatcher
        .cpp(true)
        .std("c++17")
        .warnings(false)
        .include(vpl.join("api"))
        .include(vpl.join("libvpl"))
        .include(vpl.join("libvpl/src"))
        .define("UNICODE", None)
        .define("_UNICODE", None)
        .define("NOMINMAX", None)
        .flag("/EHsc")
        .flag("/FIobjbase.h");
    for source in [
        "mfx_config_interface/mfx_config_interface.cpp",
        "mfx_config_interface/mfx_config_interface_string_api.cpp",
        "mfx_dispatcher_vpl.cpp",
        "mfx_dispatcher_vpl_config.cpp",
        "mfx_dispatcher_vpl_loader.cpp",
        "mfx_dispatcher_vpl_log.cpp",
        "mfx_dispatcher_vpl_lowlatency.cpp",
        "mfx_dispatcher_vpl_msdk.cpp",
        "windows/main.cpp",
        "windows/mfx_critical_section.cpp",
        "windows/mfx_dispatcher.cpp",
        "windows/mfx_dispatcher_log.cpp",
        "windows/mfx_driver_store_loader.cpp",
        "windows/mfx_dxva2_device.cpp",
        "windows/mfx_function_table.cpp",
        "windows/mfx_library_iterator.cpp",
        "windows/mfx_load_dll.cpp",
        "windows/mfx_win_reg_key.cpp",
    ] {
        dispatcher.file(vpl.join("libvpl/src").join(source));
    }
    dispatcher.compile("re_vpl_dispatcher");
    cc::Build::new()
        .cpp(true)
        .std("c++17")
        .flag("/EHsc")
        .define("NOMINMAX", None)
        .define("WIN32_LEAN_AND_MEAN", None)
        .include(vpl.join("api"))
        .include(root.join("../../third_party/nv-codec-headers"))
        .files(["native/bridge.cpp", "native/vpl.cpp", "native/nvenc.cpp"])
        .compile("re_encode_native");
    for library in [
        "advapi32", "cfgmgr32", "d3d11", "dxgi", "dxva2", "ole32", "version",
    ] {
        println!("cargo:rustc-link-lib={library}");
    }
    println!("cargo:rerun-if-changed=native");
    println!("cargo:rerun-if-changed={}", vpl.display());
    println!("cargo:rerun-if-changed=../../third_party/nv-codec-headers/nvEncodeAPI.h");
}
