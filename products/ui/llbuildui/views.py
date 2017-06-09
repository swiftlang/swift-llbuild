import flask
import sqlalchemy.sql
import sqlalchemy.sql.expression

from flask import Flask, current_app, g, redirect, request, session, url_for

import graphalgorithms
import model

main = flask.Blueprint('main', __name__)

@main.route('/')
def index():
    return flask.render_template("index.html")

# MARK: Trace Viewer

@main.route('/trace')
def trace_root():
    # If no database has been selected, prompt for one.
    trace_path = session.get("trace")
    if trace_path is None:
        return redirect(url_for('main.trace_config'))
    
    return flask.render_template(
        "trace_root.html", trace=current_app.trace,
        trace_path=session.get("trace"))

@main.route('/trace/config', methods=['GET', 'POST'])
def trace_config():
    if request.method == 'POST':
        session['trace'] = request.form['trace_path']
        return redirect(url_for('main.trace_root'))
    return flask.render_template("trace_config.html",
                                 trace_path=session.get("trace"))

# MARK: Database Browser

@main.route('/db')
def db_root():
    # If no database has been selected, prompt for one.
    db_path = session.get("db")
    if db_path is None:
        return redirect(url_for('main.db_config'))
    
    s = current_app.database_session

    # Compute the roots of the results.
    #
    # We compute this by simply looking for nodes which have no dependencies.
    dependees = set()
    for result in s.query(model.RuleResult):
        for item in result.dependencies:
            dependees.add(item)
    roots = [result
             for result in s.query(model.RuleResult)
             if result.key_id not in dependees]

    return flask.render_template("db_root.html", db_path=db_path, roots=roots)

@main.route('/db/config', methods=['GET', 'POST'])
def db_config():
    if request.method == 'POST':
        session['db'] = request.form['db_path']
        return redirect(url_for('main.db_root'))
    return flask.render_template("db_config.html", db_path=session.get("db"))

@main.route('/db/diagnostics', methods=['GET', 'POST'])
def db_diagnostics():
    s = current_app.database_session

    # Find all the rules.
    rules = {}
    key_names = {}
    for result in s.query(model.KeyName):
        key_names[result.id] = result.name
    for result in s.query(model.RuleResult):
        rules[result.key_id] = result

    # Find information on any cycles in the database.
    def successors(id):
        # Get the rule.
        rule = rules[id]

        # Get the dependencies.
        succs = []
        for dependency in rule.dependencies:
            succs.append(dependency)
        return succs
    keys = sorted(rules.keys())
    cycle = graphalgorithms.find_cycle(keys, successors)
    if cycle is not None:
        cycle = cycle[0].items + [cycle[1]]
        cycle = [key_names[id] for id in cycle]
    
    return flask.render_template("db_diagnostics.html",
                                 db_path=session.get("db"),
                                 session=s, model=model, sql=sqlalchemy.sql,
                                 cycle=cycle)


# MARK: Model Object Views

@main.route('/db/rule_result/<path:name>')
def db_rule_result(name):
    # Get the result.
    s = current_app.database_session
    rule_result = s.query(model.RuleResult).join(model.KeyName).filter(
        model.KeyName.name == name).one()
    dependency_results = [
        s.query(model.RuleResult).filter_by(
            key_id=dependency).one()
        for dependency in rule_result.dependencies]
    # FIXME: We need to get back the dependents view, it was super useful.
    dependency_results = sorted(dependency_results, key=lambda d: d.key.name)
    
    return flask.render_template(
        "db_rule_result.html",
        db_path=session.get("db"), rule_result=rule_result,
        dependency_results=dependency_results)
