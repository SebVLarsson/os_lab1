# Dependencies

Open a terminal and navigate to this directory:
```sh
cd <path to this directory>
```

Use `python3` to create a virtual environment.
Use Python 3.9 or later:
```sh
python3 -m venv venv
```

Install the dependencies in your new virtual environment:
```sh
# Activate the virtual environment
source ./venv/bin/activate

# Install dependencies
pip install -r ./requirements.txt
```

# Run the tests

Assuming that you're sticking to the git repo structure (i.e. you're running this from `lab_1/test`, and your lsh code is in `lab_1/code`), then `test.py` will automatically use your code:

```sh
# Activate the virtual environment
source ./venv/bin/activate

# Execute tests
python test.py
```

If your code is somewhere else for whatever reason, `test.py` will take the code directory from the `LSH_CODE` environment variable, so you can do this:

```sh
LSH_CODE=<your lsh code directory> python test.py
```

## Viewing Results

The test report is stored in an HTML file at:
`./reports/report_<date & time>/report_<date & time>.html`

After the tests finish, the HTML report should open automatically in your browser.

In some environments, such as KDE, the report may not open automatically.

# In Case of Failure

1. Identify the failing test case in the test report. Each test case has a name that starts with `test_`, such as `test_simple`.

2. Open `test.py` and find the corresponding test method, such as `def test_date(self):`.

3. Read the documentation under the method definition to understand what the test case is checking for.

4. Manually run the test case to reproduce the failure. Once you have identified the issue, fix the bug.

5. If you want to run just a single test, you can use `python test.py TestLsh.test_<test name>`.

To skip a test case—for example, if it crashes the entire test suite—decorate it with `@unittest.skip`.
