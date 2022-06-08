# River: MATLAB Bindings

Expewrimental bindings for River in MATLAB. Uses MEX files to wrap C++ library. Before using these MATLAB bindings, you need `river-cpp` installed, preferably via conda, as described in the main README.

After that, you can run the `build_all` script in the `matlab` directory, changing `PREFIX_DIR` up top as necessary to point to the right installation location for river. After this, you can run `run_tests` which writes and reads a stream to localhost to check for functionality. This `run_tests.m` script can also provide examples on which classes to use and how.

Not all functionality is implemented in these MATLAB bindings (e.g. things like `total_samples_read` or `tail()`) and can be implemented if there's demand.

Finally, note that these MATLAB bindings have not been extensively used (unlike the Python/C++ bindings, which have). Double-check any results or reach out with bugs. Thanks!
