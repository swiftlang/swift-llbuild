import flask
import sqlalchemy.sql

from flask import Flask, current_app, g, redirect, request, session, url_for

import graphalgorithms
import model

main = flask.Blueprint('main', __name__)

@main.route('/')
def index():
    # If no database has been selected, prompt for one.
    db_path = session.get("db")
    if db_path is None:
        return redirect(url_for('main.config'))
    
    s = current_app.database_session

    # Compute the roots of the results.
    #
    # We compute this by simply looking for nodes which have no dependencies.
    roots_query = s.query(model.RuleResult) \
                  .filter(model.RuleResult.key_id.notin_(
                      s.query(model.RuleDependency.key_id)))

    return flask.render_template(
        "index.html",
        db_path=db_path, roots=roots_query.all())

@main.route('/config', methods=['GET', 'POST'])
def config():
    if request.method == 'POST':
        session['db'] = request.form['db_path']
        return redirect(url_for('main.index'))
    return flask.render_template("config.html", db_path=session.get("db"))

@main.route('/diagnostics', methods=['GET', 'POST'])
def diagnostics():
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
            succs.append(dependency.key_id)
        return succs
    keys = sorted(rules.keys())
    cycle = graphalgorithms.find_cycle(keys, successors)
    if cycle is not None:
        cycle = cycle[0].items + [cycle[1]]
        cycle = [key_names[id] for id in cycle]
    
    return flask.render_template("diagnostics.html",
                                 db_path=session.get("db"),
                                 session=s, model=model, sql=sqlalchemy.sql,
                                 cycle=cycle)


# MARK: Model Object Views

@main.route('/rule_result/<path:name>')
def rule_result(name):
    # Get the result.
    s = current_app.database_session
    rule_result = s.query(model.RuleResult).join(model.KeyName).filter(
        model.KeyName.name == name).one()
    dependency_results = [
        s.query(model.RuleResult).filter_by(
            key=dependency.key).one()
        for dependency in rule_result.dependencies]
    dependents_results = s.query(model.RuleResult) \
                          .filter(model.RuleResult.id.in_(
                              s.query(model.RuleDependency.rule_id).filter_by(
                                  key=rule_result.key)))
    dependency_results = sorted(dependency_results, key=lambda d: d.key.name)
    dependents_results = sorted(dependents_results, key=lambda d: d.key.name)
    
    return flask.render_template(
        "rule_result.html",
        db_path=session.get("db"), rule_result=rule_result,
        dependency_results=dependency_results,
        dependents_results=dependents_results)
