# Docs: River

## Developing
Doc generation for C++ roughly follows this blog post: https://devblogs.microsoft.com/cppblog/clear-functional-c-documentation-with-sphinx-breathe-doxygen-cmake/. The Doxygen output is then combined with Sphinx's Autodoc functionality to generate the C++ and Python documentation (respectively).

You'll need `doxygen`, `sphinx`, and `breathe` installed. `doxygen` can be installed with your favorite package manager, and sphinx / breathe can be installed with pip and/or conda. From there, the standard CMake pipeline (`make` and `make install` should work).

```
# From the docs folder
mkdir build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ../
make
```

This looks at the *installed* versions of the Python and C++ libraries, and then extracts documentation from there. Changes are written directly into the source tree.
