# llbuild UI

This is a simple web UI for llbuild.

This is currently only intended for debugging use, and is not installed.

The UI itself is built as a Flask application and uses SQLAlchemy for directly
reading the llbuild database files. Eventually we may want to migrate to a more
production focused architecture which uses llbuild directly.

## Quick Start

You can start using the UI by setting up the web app in a local environment:

1. Install virtualenv, if necessary. For example:

~~~shell
$ easy_install --user virtualenv
~~~

2. Create a "virtual environment" to install the application in a local
   directory (e.g., `venv`):

~~~shell
$ python -m virtualenv venv
~~~

3. Install the application in the local virtual environment:

~~~shell
$ venv/bin/python setup.py develop
~~~

4. Run the application:

~~~shell
$ FLASK_APP=llbuildui.app venv/bin/python -m flask run
 * Serving Flask app "llbuildui.app"
 * Running on http://127.0.0.1:5000/ (Press CTRL+C to quit)
~~~

You can start in debug/development mode with:
~~~shell
$ FLASK_APP=llbuildui.app FLASK_DEBUG=1 venv/bin/python -m flask run
~~~
