import os
import subprocess
import sys
import shutil

from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext
from setuptools.command.install_lib import install_lib

with open('README.md', 'r') as readme_file:
    long_description = readme_file.read()


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=''):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)

ext_nested = True

class CMakeBuild(build_ext):
    def run(self):
        # Place all extensions into the Python package folder (True) or not (False)
        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))

        # required for auto-detection of auxiliary "native" libs
        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep

        if ext_nested:
            install_dir = os.path.join(os.path.abspath(ext.sourcedir), ext.name)
        else:
            install_dir = os.path.abspath(ext.sourcedir)

        build_type = os.environ.get('BUILD_TYPE', 'Release')
        cmake_args = [
            '-DCMAKE_INSTALL_PREFIX=' + install_dir,
            '-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=' + extdir,
            '-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=' + extdir,
            '-DCMAKE_BUILD_TYPE=' + build_type,
            '-GNinja'
        ]

        # A special flag that can be used in the CMake project to take actions,
        # depending on whether Python bindings are enabled or disabled.
        if False:
            cmake_args.append('-DBUILD_PYTHON_PACKAGE=ON')

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        subprocess.check_call(['cmake', ext.sourcedir] + cmake_args, cwd=self.build_temp)
        subprocess.check_call(['cmake', '--build', '.', '--config', build_type,], cwd=self.build_temp)

        # Do in-source install as well, in order to have it visible when
        # package is installed in "develop(ment) mode" (-e --editable)
        subprocess.check_call(['cmake', '--install', '.' ], cwd=self.build_temp)


class CMakeInstall(install_lib):

    def copy_tree_ignore_pycache(src, dst):
        for root, dirs, files in os.walk(src):
            # Filter out __pycache__ directories
            dirs[:] = [d for d in dirs if d != '__pycache__']
            
            # Construct the destination path
            dest_dir = os.path.join(dst, os.path.relpath(root, src))
            os.makedirs(dest_dir, exist_ok=True)
            
            # Copy files
            for file in files:
                src_file = os.path.join(root, file)
                dest_file = os.path.join(dest_dir, file)
                shutil.copy2(src_file, dest_file)

    def install(self):
        build_cmd = self.get_finalized_command('build_ext')
        build_files = build_cmd.get_outputs()
        build_temp = getattr(build_cmd, 'build_temp')

        name = self.distribution.ext_modules[0].name

        if ext_nested:
            install_dir = os.path.join(os.path.abspath(self.install_dir), name)
        else:
            install_dir = os.path.abspath(self.install_dir)

        if (os.path.exists(name)):
            CMakeInstall.copy_tree_ignore_pycache(name, install_dir)
                    
        # Adjust install prefix as shown at LLVM and not widely known:
        # https://llvm.org/docs/CMake.html#quick-start
        cmake_install_prefix = ['cmake', '-DCMAKE_INSTALL_PREFIX=' + install_dir, '-P', 'cmake_install.cmake' ]
        print("Adjusting CMake install prefix: '{}'".format(' '.join(cmake_install_prefix)))
        sys.stdout.flush()
        subprocess.check_call(cmake_install_prefix, cwd=build_temp)

        cmake_install = ['cmake', '--build', '.', '--target', 'install']
        print("Invoking CMake install: '{}'".format(' '.join(cmake_install)))
        sys.stdout.flush()
        subprocess.check_call(cmake_install, cwd=build_temp)


setup(
    name="pydisass",
    version="0.1.0",
    author='Dmitry Mikushin',
    author_email='dmitry@kernelgen.org',
    description="Raw binary ARM disassembler for embedding and Python scripting",
    long_description=long_description,
    ext_modules=[CMakeExtension("pydisass")],
    cmdclass=dict(build_ext=CMakeBuild, install_lib=CMakeInstall),
    zip_safe=False,
    python_requires='>=3.6',
    packages=find_packages(),
    include_package_data=True,
)

