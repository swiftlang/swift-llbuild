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
    
    s = current_app.database_session

    # Compute the roots of the results.
    #
    # We compute this by simply looking for nodes which have no dependencies.
    roots_query = s.query(model.RuleResult) \
                  .filter(model.RuleResult.key_id.notin_(
                      s.query(model.RuleDependency.key_id)))

    output =  """Using Database: %s (<a href="%s">change</a>)<br>\n""" % (
        db_path, url_for('main.config'))
    output += "<br>"
    output = "Roots:<br>"
    for root in roots_query:
        output += '''<a href="%s">%s</a> (id: %d)<br>''' % (
            url_for('main.rule_result', id=root.id),
            flask.escape(root.key.name),
            root.id)
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

# MARK: Model Object Views

@main.route('/rule_result/<int:id>')
def rule_result(id):
    # Get the result.
    s = current_app.database_session
    rule_result = s.query(model.RuleResult).filter_by(id=id).one()

    output = "Rule Result<br>"
    output += "Name:%s<br>" % (flask.escape(rule_result.key.name),)
    output += "<br>"

    output += "Dependencies:<br>"
    dependency_results = [
        s.query(model.RuleResult).filter_by(
            key=dependency.key).one()
        for dependency in rule_result.dependencies]
    for dependency in sorted(dependency_results, key=lambda d: d.key.name):
        # Find the result for this key.
        output += '''<a href="%s">%s</a> (id: %d)<br>''' % (
            url_for('main.rule_result', id=dependency.id),
            flask.escape(dependency.key.name),
            dependency.id)
    output += "<br>"
    
    output += "Dependents:<br>"
    dependents_results = s.query(model.RuleResult) \
                          .filter(model.RuleResult.id.in_(
                              s.query(model.RuleDependency.rule_id).filter_by(
                                  key=rule_result.key)))
    for dependent in sorted(dependents_results, key=lambda d: d.key.name):
        # Find the result for this key.
        output += '''<a href="%s">%s</a> (id: %d)<br>''' % (
            url_for('main.rule_result', id=dependent.id),
            flask.escape(dependent.key.name),
            dependent.id)
    return output
    
