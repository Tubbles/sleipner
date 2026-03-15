from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout


class SleipnerRecipe(ConanFile):
    name = "sleipner"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    def requirements(self):
        self.requires("raylib/5.5")

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
