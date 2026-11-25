fn main() {
    println!("cargo:rerun-if-changed=src/fastcopy.c");
    #[cfg(windows)]
    {
        cc::Build::new()
            .file("src/fastcopy.c")
            .compile("victory_fastcopy");
    }
}
