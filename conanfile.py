from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout, CMakeDeps
from conan.tools.env import VirtualBuildEnv
from conan.tools.env import Environment
from conan.tools.apple import XcodeDeps
import os
from sys import platform


class CppStructuraSystemsRecipe(ConanFile):
    name = "StructuraSystems"
    package_type = "application"

    # Optional metadata
    license = "GPL v3"
    author = "Moritz Herzog <herzogm@rptu.de>"
    url = "<Package recipe repository url here, for issues about the package>"
    description = "<Description of digitwester package here>"
    topics = ("<Put some tag here>", "<here>", "<and here>")

    # Binary configuration
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": True, "fPIC": False}

    # Sources are located in the same place as this recipe, copy them to the recipe
    exports_sources = "CMakeLists.txt", "structurasystems/*"

    def requirements(self):
        self.requires("boost/[>=1.86.0 <2]")
        self.requires("libcurl/[>=8.4.0 <9]")
        self.requires("nlohmann_json/[>=3.11.3 <3.13]")
        self.requires("md4c/0.5.2")
        self.requires("sysmllib/2608beta")
        self.requires("yaml-cpp/0.8.0")
        self.requires("openssl/3.6.3")
        
        if self.settings.os == "Linux":
            self.requires("qt/6.11.1")

        if self.settings.os == "Linux":
            self.requires("qt/6.11.1")

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC
            self.options.shared=True

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")
            self.options["boost/*"].shared = True
            self.options["libcurl/*"].shared = True
            self.options["nlohmann_json/*"].shared = True
            self.options["sysmllib/*"].shared=True
            self.options["yaml-cpp/*"].shared=True
            self.options["md4c/*"].shared=True
            self.options["openssl/*"].shared=True

            if self.settings.os == "Linux":
                self.options["qt/*"].shared = True

        else:
            self.options["boost/*"].shared = False
            self.options["libcurl/*"].shared = False
            self.options["nlohmann_json/*"].shared = False
            self.options["sysmllib/*"].shared=False
            self.options["yaml-cpp/*"].shared=False
            self.options["md4c/*"].shared=False
            self.options["openssl/*"].shared=False

            if self.settings.os == "Linux":
                self.options["qt/*"].shared = False

        if self.settings.os == "Linux":
            self.options["qt/*"].qtcharts = True
  

    
    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.user_presets_path = 'CMakePresets.json'
        tc.generate()
        ms = VirtualBuildEnv(self)
        ms.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def build_requirements(self):
        if self.settings.os == "Linux":
            self.tool_requires("qt/6.11.1")
        
        self.tool_requires("cmake/[>=3.30.0 <5]")


    def package(self):
        cmake = CMake(self)
        cmake.install()

    

    
