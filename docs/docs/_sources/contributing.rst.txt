============
Contributing
============

River is written in C++ primarily, with thin bindings written in Python and MATLAB. Any and all contributions are welcome, or, if feature requests are desired, please open a Github issue.

River is well-tested via both unit and integration tests, across both the reader/writer code and the ingester. To run integration tests, the tests assume Redis is running on localhost on its default port, 6379.

Regenerating Python Stubs
-------------------------
TODO: this is only in my fork?

.. code-block:: bash

  python3 /path/to/mypy/mypy/mypy/stubgen.py -p $(python3 -c "import river as _, os; print(os.path.dirname(_.__file__))") -m river && mv out/river.pyi python/```
