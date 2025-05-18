#!/usr/bin/python3

import glob
import os
import subprocess

def main():
    installed_clang_versions = _get_installed_clang_versions()
    for clang_version in installed_clang_versions:
        _remove_clang_alternative(clang_version)
        clang_alternatives = _get_clang_alternatives(clang_version)
        _install_clang_alternative(clang_version, clang_alternatives)

def _install_clang_alternative(version, alternative):
    command = [
        "update-alternatives",
        "--quiet",
        "--install",
        "/usr/bin/clang",
        "clang",
        f"/usr/bin/clang-{version}",
        version
    ] + alternative.split()

    _try_run(command)

def _remove_clang_alternative(version):
    command = [
        "update-alternatives",
        "--remove",
        "clang",
        f"/usr/bin/clang-{version}"
    ]

    _try_run(command)

def _try_run(command):
    try:
        subprocess.run(command, check=True)
    except subprocess.CalledProcessError:
        print(f"Failed to execute command {command}")

def _get_clang_alternatives(version):
    list_of_llvm_binaries_paths = _get_list_of_llvm_binaries_paths(version)
    slave_alternatives = ""
    for llvm_binary_path in list_of_llvm_binaries_paths:
        slave_alternatives += _create_slave_alternative_parameter(
            llvm_binary_path, version
        )

    return slave_alternatives

def _create_slave_alternative_parameter(llvm_binary_path, version):
    llvm_binary_name = os.path.basename(llvm_binary_path)
    if not _is_executable(llvm_binary_path) or llvm_binary_name == "clang":
        return ""

    symlink = f"/usr/bin/{llvm_binary_name}-{version}"
    if not _is_executable(symlink):
        return ""

    return f"--slave /usr/bin/{llvm_binary_name} {llvm_binary_name} {symlink} "

def _get_list_of_llvm_binaries_paths(version):
    if os.path.exists(f"/lib/llvm-{version}"):
        return glob.glob(f"/lib/llvm-{version}/bin/*")

    _remove_clang_alternative(version)
    return []

def _is_executable(file_path):
    return os.path.isfile(file_path) and os.access(file_path, os.X_OK)

def _get_installed_clang_versions():
    llvm_dir_list = glob.glob("/lib/llvm-*")
    return [
        os.path.basename(llvm_dir).split("-")[1]
        for llvm_dir in llvm_dir_list
    ]

if __name__ == "__main__":
    main()

