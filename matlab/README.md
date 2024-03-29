# River: MATLAB Bindings

Expewrimental bindings for River in MATLAB. Uses MEX files to wrap C++ library. Before using these MATLAB bindings, you need `river-cpp` installed, preferably via conda, as described in the main README.

Then clone this repository:

```
git clone git@github.com:pbotros/river.git
```

Open MATLAB and cd into `river/matlab`.

After that, you can run the `build_all` script, after first changing `PREFIX_DIR` up top as necessary to point to the right installation location for river. Once built successfully, you can run `run_tests` which writes and reads a stream to localhost Redis to check for functionality. The tests in the `tests` subdirectory serve as examples on which classes to use and how.

Not all functionality is implemented in these MATLAB bindings (e.g. things like `total_samples_read` or `tail()`) and can be implemented if there's demand.

Finally, note that these MATLAB bindings have not been extensively used (unlike the Python/C++ bindings, which have). Double-check any results or reach out with bugs. Thanks!
