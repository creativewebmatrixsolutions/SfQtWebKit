%modules = ( # path to module name map
    "Enginio" => "$basedir/src/enginio_client"
);
%moduleheaders = ( # restrict the module headers to those found in relative path
);
# Module dependencies.
# Every module that is required to build this module should have one entry.
# Each of the module version specifiers can take one of the following values:
#   - A specific Git revision.
#   - any git symbolic ref resolvable from the module's repository (e.g. "refs/heads/master" to track master branch)
#   - an empty string to use the same branch under test (dependencies will become "refs/heads/master" if we are in the master branch)
#
%dependencies = (
    "qtbase" => "refs/heads/5.5",
    "qtdeclarative" => "refs/heads/5.5",
    "qtxmlpatterns" => "refs/heads/5.5",
);

