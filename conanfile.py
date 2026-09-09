from conan import ConanFile
from conan.tools.build import cross_building
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class DdsAbstractConan(ConanFile):
    name = "dds_abstract"
    version = "1.0.0"
    package_type = "library"
    license = "Proprietary"
    description = "Protocol-independent ZeroMQ and Zenoh communication library"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_zeromq": [True, False],
        "with_zenoh": [True, False],
        "python_bindings": [True, False],
    }
    default_options = {
        "shared": True,
        "fPIC": True,
        "with_zeromq": True,
        "with_zenoh": True,
        "python_bindings": False,
    }
    exports_sources = (
        "CMakeLists.txt",
        "README.md",
        "cmake/*",
        "include/*",
        "python/*",
        "src/*",
        "tests/*",
    )

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def requirements(self):
        if self.options.with_zeromq:
            self.requires("cppzmq/4.11.0")
        if self.options.python_bindings:
            self.requires("pybind11/2.13.6")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        dependencies = CMakeDeps(self)
        dependencies.generate()
        toolchain = CMakeToolchain(self)
        toolchain.variables["BUILD_SHARED_LIBS"] = self.options.shared
        toolchain.variables["DDS_ABSTRACT_BUILD_TESTS"] = not cross_building(self)
        toolchain.variables["DDS_ABSTRACT_BUILD_PYTHON"] = (
            self.options.python_bindings
        )
        toolchain.variables["DDS_ABSTRACT_WITH_ZEROMQ"] = self.options.with_zeromq
        toolchain.variables["DDS_ABSTRACT_WITH_ZENOH"] = self.options.with_zenoh
        toolchain.variables["DDS_ABSTRACT_REQUIRE_BACKENDS"] = True
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        if not cross_building(self):
            cmake.test()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["dds_abstract"]
        self.cpp_info.set_property("cmake_file_name", "dds_abstract")
        self.cpp_info.set_property("cmake_target_name", "dds::dds_abstract")
