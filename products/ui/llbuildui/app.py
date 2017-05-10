import os

import flask

import database
import trace
import views

class LLBuildApp(flask.Flask):
    def __init__(self, name):
        super(LLBuildApp, self).__init__(name)

        # Register the local proxy.
        
        # Register the teardown functions.
        @self.teardown_appcontext
        def _teardown(exception):
            db = flask.g.get('_database_session', None)
            if db is not None:
                db.close()

        self.register_blueprint(views.main)

    @property
    def trace(self):
        d = flask.g.get('_trace', None)
        if d is None:
            result = trace.Trace.frompath(flask.session["trace"])
            d = flask.g._trace = result
        return d

    @property
    def database_session(self):
        s = flask.g.get('_database_session', None)
        if s is None:
            # This session will be closed by the teardown function we register.
            db = database.Database.get_database(flask.session["db"])
            s = flask.g._database_session = db.session_factory()
        return s

app = LLBuildApp(__name__)

# Set the secret key.
#
# We never expect this app to persist, so we just assign a new one each time.
app.secret_key = "DEBUG" if app.debug else os.urandom(24)
