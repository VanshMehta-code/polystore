# Tests

Make sure the server is running first:

```bash
./polystore --server
```

Then in a new terminal:

```bash
cd test
uv init
uv add requests
uv run test_all_engines.py
```

Or with plain pip:

```bash
pip install requests
python test_all_engines.py
```

The test creates databases for all four engines, runs every operation, tests auth and permissions, and prints each result with a label so you can see exactly what passed and what failed.
