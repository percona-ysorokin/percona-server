#!/usr/bin/env python
# Protocol Buffers - Google's data interchange format
# Copyright 2008 Google Inc.  All rights reserved.
# https://developers.google.com/protocol-buffers/
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#   * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#   * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#   * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Script to generate a list of all modules to use in autosummary.

This script creates a ReStructured Text file for each public module in the
protobuf Python package. The script also updates the table of contents in
``docs/index.rst`` to point to these module references.

To build the docs with Sphinx:

1. Install the needed packages (``sphinx``, ``sphinxcontrib-napoleon`` for
   Google-style docstring support). I've created a conda environment file to
   make this easier:

.. code:: bash

   conda env create -f python/docs/environment.yml

2. (Optional) Generate reference docs files and regenerate index:

.. code:: bash

   cd python/docs
   python generate_docs.py

3. Run Sphinx.

.. code:: bash

   make html
"""

import pathlib
import re


DOCS_DIR = pathlib.Path(__file__).parent.resolve()
PYTHON_DIR = DOCS_DIR.parent
SOURCE_DIR = PYTHON_DIR / "google" / "protobuf"
SOURCE_POSIX = SOURCE_DIR.as_posix()

# Modules which are always included:
INCLUDED_MODULES = (
  "google.protobuf.internal.containers",
)

# Packages to ignore, including all modules (unless in INCLUDED_MODULES):
IGNORED_PACKAGES = (
  "compiler",
  "docs",
  "internal",
  "pyext",
  "util",
)

# Ignored module stems in all packages (unless in INCLUDED_MODULES):
IGNORED_MODULES = (
  "any_test_pb2",
  "api_pb2",
  "unittest",
  "source_context_pb2",
  "test_messages_proto3_pb2",
  "test_messages_proto2",
)

TOC_REGEX = re.compile(
  r"\.\. START REFTOC.*\.\. END REFTOC\.\n",
  flags=re.DOTALL,
)
TOC_TEMPLATE = """.. START REFTOC, generated by generate_docs.py.
.. toctree::

   {toctree}

.. END REFTOC.
"""

AUTOMODULE_TEMPLATE = """.. DO NOT EDIT, generated by generate_docs.py.

.. ifconfig:: build_env == 'readthedocs'

   .. warning::

      You are reading the documentation for the `latest committed changes
      <https://github.com/protocolbuffers/protobuf/tree/main/python>`_ of
      the `Protocol Buffers package for Python
      <https://developers.google.com/protocol-buffers/docs/pythontutorial>`_.
      Some features may not yet be released. Read the documentation for the
      latest released package at `googleapis.dev
      <https://googleapis.dev/python/protobuf/latest/>`_.

{module}
{underline}

.. automodule:: {module}
   :members:
   :inherited-members:
   :undoc-members:
"""


def find_modules():
  modules = []
  for module_path in SOURCE_DIR.glob("**/*.py"):
    # Determine the (dotted) relative package and module names.
    package_path = module_path.parent.relative_to(PYTHON_DIR)
    if package_path == SOURCE_DIR:
      package_name = ""
      module_name = module_path.stem
    else:
      package_name = package_path.as_posix().replace("/", ".")
      module_name = package_name + "." + module_path.stem

    # Filter: first, accept anything in the whitelist; then, reject anything
    # at package level, then module name level.
    if any(include == module_name for include in INCLUDED_MODULES):
      pass
    elif any(ignored in package_name for ignored in IGNORED_PACKAGES):
      continue
    elif any(ignored in module_path.stem for ignored in IGNORED_MODULES):
      continue

    if module_path.name == "__init__.py":
      modules.append(package_name)
    else:
      modules.append(module_name)

  return modules


def write_automodule(module):
  contents = AUTOMODULE_TEMPLATE.format(module=module, underline="=" * len(module),)
  automodule_path = DOCS_DIR.joinpath(*module.split(".")).with_suffix(".rst")
  try:
    automodule_path.parent.mkdir(parents=True)
  except FileExistsError:
    pass
  with open(automodule_path, "w") as automodule_file:
    automodule_file.write(contents)


def replace_toc(modules):
  toctree = [module.replace(".", "/") for module in modules]
  with open(DOCS_DIR / "index.rst", "r") as index_file:
    index_contents = index_file.read()
  toc = TOC_TEMPLATE.format(
    toctree="\n   ".join(toctree)
  )
  index_contents = re.sub(TOC_REGEX, toc, index_contents)
  with open(DOCS_DIR / "index.rst", "w") as index_file:
    index_file.write(index_contents)


def main():
  modules = list(sorted(find_modules()))
  for module in modules:
    print("Generating reference for {}".format(module))
    write_automodule(module)
  print("Generating index.rst")
  replace_toc(modules)

if __name__ == "__main__":
    main()
