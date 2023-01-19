============
Contributing
============

River is written in C++ primarily, with thin bindings written in Python and MATLAB. Any and all contributions are welcome, or, if feature requests are desired, please open a Github issue.

Testing
^^^^^^^

River is well-tested via both unit and integration tests, across both the reader/writer code and the ingester. To run integration tests, the tests assume Redis is running on localhost on its default port, 6379. Tests are built as part of the standard CMake build process; they utilize Google Test (gtest) and can be run either manually or through a supporting IDE.

Tests on the core C++ library, i.e. without ingestion, can be found in `cpp/src/tests`, with the main entry point test in `cpp/src/tests/river_test.cpp`. To run these tests manually:

.. code-block:: bash

  cd /path/to/river/repo
  cd cpp
  mkdir -p build/debug
  cd build/debug
  cmake -G "Unix Makefiles" -DRIVER_BUILD_INGESTER=0 -DCMAKE_BUILD_TYPE=Debug ../..
  make
  cd src
  ./river_test


Similarly, if you're building the ingester (same as above but with RIVER_BUILD_INGESTER=1), then you can run `./ingester_test` in `build/debug/ingester/src`.

Note here we build as Debug for testing as it's more amenable to use with debuggers (e.g. `gdb`), but of course you can build as Release if you want too.


Regenerating Python Stubs
-------------------------
TODO: this is only in my fork?

.. code-block:: bash

  python3 /path/to/mypy/mypy/mypy/stubgen.py -p $(python3 -c "import river as _, os; print(os.path.dirname(_.__file__))") -m river && mv out/river.pyi python/```
