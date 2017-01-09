import sqlalchemy
import sqlalchemy.orm

class Database(object):
    """
    The base engine encapsulating access to a particular database.
    """

    # Get a cached database instance.
    @classmethod
    def get_database(cls, path):
        db = cls._databases.get(path)
        if db is None:
            cls._databases[path] = db = Database(path)
        return db
    _databases = {}
    
    def __init__(self, db_path):
        self.engine = sqlalchemy.create_engine("sqlite:///" + db_path)
        self.session_factory = sqlalchemy.orm.sessionmaker(bind=self.engine)
