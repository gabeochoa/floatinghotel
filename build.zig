const std = @import("std");

const warnings = [_][]const u8{
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Wuninitialized",
    "-Wshadow",
    "-Wconversion",
    "-Wcast-qual",
    "-Wchar-subscripts",
    "-Wcomment",
    "-Wdisabled-optimization",
    "-Wformat=2",
    "-Wformat-nonliteral",
    "-Wformat-security",
    "-Wformat-y2k",
    "-Wimport",
    "-Winit-self",
    "-Winline",
    "-Winvalid-pch",
    "-Wlong-long",
    "-Wmissing-format-attribute",
    "-Wmissing-include-dirs",
    "-Wpacked",
    "-Wpointer-arith",
    "-Wreturn-type",
    "-Wsequence-point",
    "-Wstrict-overflow=5",
    "-Wswitch",
    "-Wswitch-enum",
    "-Wtrigraphs",
    "-Wunused-label",
    "-Wunused-parameter",
    "-Wunused-value",
    "-Wunused-variable",
    "-Wvariadic-macros",
    "-Wvolatile-register-var",
    "-Wwrite-strings",
    "-Warray-bounds",
    "-Wno-deprecated-volatile",
    "-Wno-missing-field-initializers",
    "-Wno-c99-extensions",
    "-Wno-unused-function",
    "-Wno-sign-conversion",
    "-Wno-implicit-int-float-conversion",
    "-Wno-implicit-float-conversion",
    "-Wno-format-nonliteral",
    "-Wno-format-security",
    "-Wno-format-y2k",
    "-Wno-import",
    "-Wno-inline",
    "-Wno-invalid-pch",
    "-Wno-long-long",
    "-Wno-missing-format-attribute",
    "-Wno-missing-noreturn",
    "-Wno-packed",
    "-Wno-redundant-decls",
    "-Wno-sequence-point",
    "-Wno-trigraphs",
    "-Wno-variadic-macros",
    "-Wno-volatile-register-var",
};

pub fn build(b: *std.Build) void {
    b.resolveInstallPrefix(b.pathFromRoot("output"), .{});
    // -O2 -g keeps crash reports symbolicated and shrinks the binary ~5x, which is
    // what exec-to-main and the per-relink security rescan on this machine scale with.
    const opt = b.option([]const u8, "opt", "Optimisation level: 0, 1, 2, 3, s, g") orelse "2";
    // Oldest macOS the binary runs on. Unset, zig stamps the build machine's own
    // version (minos), so nothing older can launch it.
    const macos_min = b.option([]const u8, "macos-min", "Minimum macOS version, e.g. 14.0");
    const cxx: []const []const u8 = if (b.option([]const u8, "cxx", "C++ compiler (default: zig c++)")) |c| &.{c} else &.{ b.graph.zig_exe, "c++" };

    var flags: std.ArrayList([]const u8) = .empty;
    flags.appendSlice(b.allocator, &warnings) catch @panic("oom");
    flags.appendSlice(b.allocator, &.{
        "-std=c++23",                       "-g",                b.fmt("-O{s}", .{opt}),                "-pipe",                              "-fno-stack-protector",               "-fno-common",
        // zig c++ defaults to Debug, which turns UBSan on. Its null check trapped in
        // afterhours' singleton lookups, and the instrumentation was most of a 32 MB binary.
        "-fno-sanitize=undefined",
        // Trackpad pinch: afterhours' NSEvent monitor is a block.
                 "-fblocks",          "-DAFTER_HOURS_ENABLE_MACOS_GESTURES", "-DAFTERHOURS_ENFORCE_MIN_FONT_SIZE", "-DAFTER_HOURS_UI_SINGLE_COLLECTION", "-DAFTER_HOURS_USE_METAL",
        "-DAFTER_HOURS_ENABLE_E2E_TESTING", "-DFMT_HEADER_ONLY",
    }) catch @panic("oom");
    // Optimised builds are releases: development UI validation is off there
    // (it costs per-frame time); debug builds and --perf control it at runtime.
    if (!std.mem.eql(u8, opt, "0") and !std.mem.eql(u8, opt, "g"))
        flags.append(b.allocator, "-DFH_RELEASE_BUILD") catch @panic("oom");
    // Per-system profiling (dump_profile / bench_frames top systems) in
    // non-release builds, or any build with -Dprofile=true (the perf probe
    // build: `zig build -Dprofile=true` keeps -O2 and adds the hooks). The
    // collector records only when test mode starts it, so a plain release
    // binary carries neither the hooks nor the cost.
    const profile = b.option(bool, "profile", "Enable per-system profiling hooks") orelse false;
    if (profile or std.mem.eql(u8, opt, "0") or std.mem.eql(u8, opt, "g"))
        flags.append(b.allocator, "-DAFTERHOURS_ENABLE_PROFILING=1") catch @panic("oom");
    var link_extra: []const []const u8 = &.{};
    if (macos_min) |min| {
        // zig needs the SDK paths spelled out once it is given an explicit target.
        const sdk = std.mem.trim(u8, b.run(&.{ "xcrun", "--show-sdk-path" }), " \n");
        flags.appendSlice(b.allocator, &.{
            "-target",  b.fmt("aarch64-macos.{s}", .{min}), "-isysroot",   sdk,
            "-isystem", b.fmt("{s}/usr/include", .{sdk}),   "-iframework", b.fmt("{s}/System/Library/Frameworks", .{sdk}),
        }) catch @panic("oom");
        link_extra = b.dupeStrings(&.{ b.fmt("-F{s}/System/Library/Frameworks", .{sdk}), b.fmt("-L{s}/usr/lib", .{sdk}) });
    }

    const link = b.addSystemCommand(cxx);
    link.setName("link floatinghotel");
    link.addArgs(flags.items);
    var sokol_obj: ?std.Build.LazyPath = null;
    for (sources(b)) |src| {
        const obj = compile(b, cxx, src, flags.items);
        if (std.mem.eql(u8, src, "src/sokol_impl.mm")) sokol_obj = obj;
        link.addFileArg(obj);
    }
    link.addArg("-Wl,-dead_strip");
    link.addArgs(link_extra);
    for ([_][]const u8{ "CoreFoundation", "CoreServices", "Metal", "MetalKit", "Cocoa", "QuartzCore", "AppKit", "ImageIO", "CoreGraphics" }) |fw| link.addArgs(&.{ "-framework", fw });
    link.addArg("-o");
    const exe = link.addOutputFileArg("floatinghotel.exe");

    const install = b.getInstallStep();
    install.dependOn(&b.addInstallFile(exe, "floatinghotel.exe").step);
    install.dependOn(&b.addInstallDirectory(.{ .source_dir = b.path("resources"), .install_dir = .prefix, .install_subdir = "resources" }).step);
    // tests/check_native_*_resize.sh link their harness against this object.
    install.dependOn(&b.addInstallFile(sokol_obj.?, "objs/sokol_impl.o").step);

    const run = command(b, &.{b.pathFromRoot("output/floatinghotel.exe")}, install);
    run.addArgs(b.args orelse &.{"."});
    b.step("run", "Build and run; pass app arguments after -- (default: .)").dependOn(&run.step);

    const registry = command(b, &.{ "python3", "scripts/check_unit_registry.py" }, null);
    const tests = command(b, &.{ "bash", "tests/run_unit_tests.sh" }, &registry.step);
    if (b.args) |args| tests.addArgs(args);
    b.step("test", "Run unit tests (tests/run_unit_tests.sh is the registry)").dependOn(&tests.step);

    const bundle = command(b, &.{ "bash", "scripts/bundle.sh" }, install);
    if (macos_min) |min| bundle.setEnvironmentVariable("MACOS_MIN", min);
    b.step("bundle", "Build output/FloatingHotel.app").dependOn(&bundle.step);
    b.step("validate", "Run the UI audit against the fixture repo (needs GPU)").dependOn(&command(b, &.{ "bash", "scripts/validate.sh" }, install).step);
    b.step("install-app", "Link ~/.local/bin/fh to output/floatinghotel.exe").dependOn(&command(b, &.{ "bash", "scripts/install.sh" }, install).step);
}

// Every .cpp/.mm under src/, so a new file can't be left out of the build.
fn sources(b: *std.Build) []const []const u8 {
    const io = b.graph.io;
    var list: std.ArrayList([]const u8) = .empty;
    var dir = b.build_root.handle.openDir(io, "src", .{ .iterate = true }) catch @panic("open src/");
    defer dir.close(io);
    var walker = dir.walk(b.allocator) catch @panic("oom");
    defer walker.deinit();
    while (walker.next(io) catch @panic("walk src/")) |entry| {
        if (entry.kind != .file) continue;
        if (!std.mem.endsWith(u8, entry.path, ".cpp") and !std.mem.endsWith(u8, entry.path, ".mm")) continue;
        list.append(b.allocator, b.fmt("src/{s}", .{entry.path})) catch @panic("oom");
    }
    list.append(b.allocator, "vendor/afterhours/src/plugins/files.cpp") catch @panic("oom");
    return list.items;
}

fn compile(b: *std.Build, cxx: []const []const u8, src: []const u8, flags: []const []const u8) std.Build.LazyPath {
    const r = b.addSystemCommand(cxx);
    r.setName(b.fmt("compile {s}", .{src}));
    if (std.mem.endsWith(u8, src, ".mm")) {
        // ARC: the vendored capture_impl.h relies on it to release its readback
        // textures (without it the E2E suite leaked past 1 GB of GPU memory).
        // native_menu.mm does its own retain/release.
        r.addArgs(&.{ "-ObjC++", if (std.mem.endsWith(u8, src, "native_menu.mm")) "-fno-objc-arc" else "-fobjc-arc" });
    }
    r.addArgs(flags);
    // files.cpp is a source, not an -isystem include, so its warnings would leak.
    if (std.mem.startsWith(u8, src, "vendor/")) r.addArg("-w");
    r.addArgs(&.{ "-isystem", b.pathFromRoot("vendor"), "-isystem", b.pathFromRoot("vendor/afterhours/vendor"), "-c" });
    r.addFileArg(b.path(src));
    r.addArg("-o");
    const stem = std.fs.path.stem(src);
    const obj = r.addOutputFileArg(b.fmt("{s}.o", .{stem}));
    // -MD, not -MMD: afterhours comes in via -isystem, and -MMD would drop its
    // headers from the depfile, so a submodule bump would leave stale objects.
    r.addArgs(&.{ "-MD", "-MF" });
    _ = r.addDepFileOutputArg(b.fmt("{s}.d", .{stem}));
    return obj;
}

fn command(b: *std.Build, args: []const []const u8, after: ?*std.Build.Step) *std.Build.Step.Run {
    const r = b.addSystemCommand(args);
    r.setCwd(b.path("."));
    r.has_side_effects = true;
    if (after) |step| r.step.dependOn(step);
    return r;
}
