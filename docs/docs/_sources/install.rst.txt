============
Installation
============

Pre-packaged
------------

For the C++ library, installation via Conda is the preferred way:

.. code-block:: bash

  conda install -c conda-forge river-cpp

For installing the ingester, it's a separate conda install as well: **NOTE: the ingester binary isn't available on Windows due to compilation issues. Use WSL or Linux/Mac for the ingester**.

.. code-block:: bash

  conda install -c conda-forge river-ingester

For the Python bindings, conda is also preferred:

.. code-block:: bash

  conda install -c conda-forge river-py

For the (experimental) MATLAB bindings, see the MATLAB README for details.


CMake
-----
The above instructions install River's C++ libraries, binaries, and/or Python bindings into standard system directories. This process also installs the appropriate pkg-config to enable CMake in your project to find River in a standard fashion. If you want to use River in your own C++ CMake project, you should be able to do:

.. code-block:: cmake

  find_package(river REQUIRED)
  ...
  target_link_libraries(my_target PRIVATE river::river)


Compiling from Source
---------------------
You can also compile by source and install manually. The below steps will compile the C++ library and install both the C++ library/headers and the Python bindings. This project uses CMake.

Prerequisites
^^^^^^^^^^^^^

In order to build the core C++ library for River, the only package expected to be installed should be Google Log (glog). 

If you're building the Python bindings, then you'll need Python 3.7+. If you're also building and installing the Ingester, you'll need:
- Boost 1.67+
- Apache Arrow and Parquet

Use your favorite package manager to install the above. For instance, if you use conda, run:

.. code-block:: bash

  conda install -c conda-forge pkg-config cmake # build tools
  conda install -c conda-forge glog  # Google Log
  conda install -c conda-forge boost-cpp arrow-cpp # Boost/Arrow/Parquet, if installing ingester

Installing
^^^^^^^^^^

Since River uses CMake, you can use standard CMake commands such as (if on Mac or Linux):

.. code-block:: bash

  git clone git@github.com:pbotros/river.git
  cd river/cpp
  mkdir -p build/release
  cd build/release
  cmake -G "Unix Makefiles" -DRIVER_BUILD_INGESTER=0 -DCMAKE_BUILD_TYPE=Release ../..
  make
  sudo make install  # if on Mac, can omit sudo
  sudo ldconfig  # if on Linux

Replace `{r,R}elease` with `{d,D}ebug` in the above to build debug binaries with debugging symbols if needed. If on Windows, you can use the CMake GUI, or replace the "-G" command with the appropriate identifier (e.g. `-G "Visual Studio 15 2017"`).

By default, building the ingester is *NOT* enabled, as a typical system configuration will have many readers and writers distributed across a variety of computers but a single instance of ingestion running on a local computer. 

To enable building the ingester, enable the CMake flag `RIVER_BUILD_INGESTER` as in the following example:

.. code-block:: bash

  cmake -DCMAKE_BUILD_TYPE=Release -DRIVER_BUILD_INGESTER=1 -G "CodeBlocks - Unix Makefiles" ../..
  make
  sudo make install

This will build and install a `river-ingester` binary in your default install path, e.g., `/usr/local/bin/` on modern Mac/Unix systems. Run the `river-ingester` binary with the `--help` option for more details.

Finally, to build and install the Python bindings, you can take similar steps:

.. code-block:: bash

  cd /path/to/river/repo
  cd python
  mkdir -p build/release
  cd build/release
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ../..
  make
  sudo make install  # if on Mac, can omit sudo

This should install it in the default installation site. To pass a custom python3 executable in the case of cross-compilation or when manually handling different python3 versions, you can invoke `cmake` with the `-DPython3_EXECUTABLE=/path/to/custom/python3` to override the Python used.

Verifying Installation
^^^^^^^^^^^^^^^^^^^^^^

To test whether the installation was correct, run the benchmark, assuming you're running Redis on localhost:

.. code-block:: bash

  # From the root of the river repository
  cd cpp/build/release/src
  ./river_benchmark --redis_hostname 127.0.0.1  --batch_size 1 --sample_size 128 --num_samples 1000

Exact results of the River benchmark will vary depending on the batch size, sample size, number of samples, and your hardware.


Troubleshooting
^^^^^^^^^^^^^^^

Installing Google Log (GLOG)
""""""""""""""""""""""""""""

On Mac, `brew install glog` seems to work fine to resolve dependencies needed for Google Log. However, on other distros where the version of GLOG is too old and doesn't include a CMakeLists.txt (i.e. Raspbian Buster, Ubuntu 18.04), GLOG needs to be compiled and installed from source. Note that, alternatively, conda via conda-forge might have a sufficient version of `glog`.

.. code-block:: bash

  cd /some/directory
  git clone https://github.com/google/glog.git
  cd glog
  mkdir build
  cd build
  cmake -DCMAKE_BUILD_TYPE=Release -G "CodeBlocks - Unix Makefiles" -DBUILD_SHARED_LIBS=ON ..
  make
  sudo make install

If you get an error like `ERROR: flag 'logtostderr' was defined more than once (in files 'src/logging.cc' and '/some/path/to/logging.cc').`, then you might have multiple installations of GLOG / GFlags. To fix this, you can have CMake build GLOG from source instead of relying on your system versions of GLOG. Do this by uninstalling glog:

.. code-block:: bash

  sudo apt remove libgflags-dev libglog-dev

Installing Boost on Linux
"""""""""""""""""""""""""
In some Linux distributions, the packaged version of Boost might be too old. If you're using conda, conda-forge should have an updated version. If you're not, you'll have to install via source. In order to install Boost from source, follow [the Boost website](https://www.boost.org/doc/libs/1_57_0/more/getting_started/unix-variants.html). In particular, the following commands will install the libraries needed, once you've downloaded the most recent release and un-tar'd it:

.. code-block:: bash

  ./bootstrap --with-libraries=filesystem,graph,program_options,system,headers,thread
  ./b2
  sudo ./b2 install


Installing Boost on Windows
"""""""""""""""""""""""""""
Boost can be installed via a precompiled binary posted by the boost team. [Go here](https://sourceforge.net/projects/boost/files/boost-binaries) to find the latest precompiled Boost binaries. You can also install via conda.


