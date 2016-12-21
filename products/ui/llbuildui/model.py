from sqlalchemy import *
from sqlalchemy.orm import relation, relationship
from sqlalchemy.ext.declarative import declarative_base

# DB Declaration

Base = declarative_base()

class KeyName(Base):
    __tablename__ = "key_names"

    id = Column(Integer, nullable=False, primary_key=True)
    key = Column(String, nullable=False)

    def __repr__(self):
        return "%s%r" % (
            self.__class__.__name__, (self.id, self.key))

class RuleResult(Base):
    __tablename__ = "rule_results"

    id = Column(Integer, nullable=False, primary_key=True)
    key_id = Column(Integer, ForeignKey(KeyName.id),
                     nullable=False)
    value = Column(Integer, nullable=False)
    built_at = Column(Integer, nullable=False)
    computed_at = Column(Integer, nullable=False)

    key = relation(KeyName)
    dependencies = relationship('RuleDependency')

    def __repr__(self):
        return "%s%r" % (
            self.__class__.__name__, (self.id, self.key, self.value,
                                      self.built_at, self.computed_at))

class RuleDependency(Base):
    __tablename__ = "rule_dependencies"

    rule_id = Column(Integer, ForeignKey(RuleResult.id),
                     nullable=False, primary_key=True)
    key_id = Column(Integer, ForeignKey(KeyName.id),
                     nullable=False, primary_key=True)

    key = relation(KeyName)

    def __repr__(self):
        return "%s%r" % (
            self.__class__.__name__, (self.rule_id, self.key))
