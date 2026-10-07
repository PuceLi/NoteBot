add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")

option("target_type")
    set_default("client")
    set_showmenu(true)
    set_values("server", "client")
option_end()

-- add_requires("levilamina x.x.x") for a specific version
-- add_requires("levilamina develop") to use develop version
-- please note that you should add bdslibrary yourself if using dev version
add_requires("levilamina 26.51.6", {configs = {target_type = get_config("target_type")}})
add_requires("imgui v1.92.9", {configs = {shared = false, win32 = true, dx11 = true, dx12 = true}})
add_requires("minhook", {configs = {shared = false}})
add_requires("levibuildscript")

if is_plat("windows") then
    add_syslinks("d3d11", "d3d12", "dxgi", "gdiplus")
end

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("NoteBot") -- Change this to your mod name.
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE")
        set_exceptions("none") -- To avoid conflicts with /EHa.
        add_cxflags( "/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "/EHs",
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            {tools = {"clang_cl"}}
        )
        set_toolchains("clang-cl")
    end
    add_packages("levilamina", "imgui", "minhook")
    set_kind("shared")
    set_languages("c++20")
    set_symbols("debug")
    add_cxxflags("/Zc:__cplusplus")
    add_headerfiles("src/**.h")
    add_files("src/**.cpp")
    add_includedirs("src")
    after_build(function (target)
        local font = path.join(os.projectdir(), "src", "ui", "Comfortaa.ttf")
        local mod_data = path.join(os.projectdir(), "bin", "NoteBot", "data")
        os.mkdir(mod_data)
        os.cp(font, path.join(mod_data, "Comfortaa.ttf"))
        local icons = path.join(mod_data, "ui")
        os.mkdir(icons)
        for _, icon in ipairs({"reset.png", "copy.png", "paste.png"}) do
            os.cp(path.join(os.projectdir(), "src", "ui", "assets", icon),
                  path.join(icons, icon))
        end
    end)
    if is_config("target_type", "server") then
    --  add_includedirs("src-server")
    --  add_files("src-server/**.cpp")
    else
    --  add_includedirs("src-client")
    --  add_files("src-client/**.cpp")
    end
