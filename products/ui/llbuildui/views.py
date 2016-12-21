import flask

from flask import Flask, current_app, g, redirect, request, session, url_for

import model

main = flask.Blueprint('main', __name__)

@main.route('/')
def index():
    # If no database has been selected, prompt for one.
    db_path = session.get("db")
    if db_path is None:
        return redirect(url_for('main.config'))
    
    # Check if we have a database.
    output =  """Using Database: %s (<a href="%s">change</a>)<br>\n""" % (
        db_path, url_for('main.config'))

    s = current_app.database_session
    for result in s.query(model.KeyName).limit(1000):
        output += "%s<br>\n" % (flask.escape(result),)
    return output

@main.route('/config', methods=['GET', 'POST'])
def config():
    if request.method == 'POST':
        session['db'] = request.form['db_path']
        return redirect(url_for('main.index'))
    return '''
Select the database:
    <form action="" method="post">
            <p><input type=text name=db_path>
            <p><input type=submit value="Set Path">
        </form>
    '''
