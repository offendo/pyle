import os
import sys
import pathlib
import subprocess
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        super().__init__(name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuildExt(build_ext):
    def run(self):
        for ext in self.extensions:
            self.build_cmake(ext)

    def build_cmake(self, ext):
        extdir = pathlib.Path(self.get_ext_fullpath(ext.name)).parent.resolve()
        build_temp = pathlib.Path(self.build_temp)
        build_temp.mkdir(parents=True, exist_ok=True)

        print(f"🏗️  Building {ext.name} using CMake...")

        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}",
            f"-DPYTHON_EXECUTABLE={sys.executable}",
            "-DCMAKE_BUILD_TYPE=Release",
        ]

        build_args = ["--config", "Release"]

        # Configure
        subprocess.check_call(["cmake", ext.sourcedir] + cmake_args, cwd=build_temp)
        # Build
        subprocess.check_call(["cmake", "--build", ".", "--target", ext.name] + build_args, cwd=build_temp)


setup(
    name="pyl",
    version="0.1.0",
    author="Nilay Patel",
    author_email="nilaypatel2@gmail.com",
    description="Python bindings for a Lean 4 interpreter",
    long_description="Python bindings for a Lean 4 interpreter.",
    ext_modules=[CMakeExtension("pyl", sourcedir=".")],
    cmdclass={"build_ext": CMakeBuildExt},
    zip_safe=False,
    python_requires=">=3.8",
)
