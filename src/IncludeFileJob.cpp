/* This file is part of RTags (http://rtags.net).

RTags is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTags is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTags.  If not, see <http://www.gnu.org/licenses/>. */

#include "IncludeFileJob.h"
#include "RTags.h"
#include "Server.h"
#include "Project.h"

IncludeFileJob::IncludeFileJob(const std::shared_ptr<QueryMessage> &query, const std::shared_ptr<Project> &project)
    : QueryJob(query, project)
{
    const uint32_t fileId = Location::fileId(query->currentFile());
    mSource = project->sources(fileId).value(query->buildIndex());
    if (mSource.isNull()) {
        for (const uint32_t dep : project->dependencies(fileId, Project::DependsOnArg)) {
            mSource = project->sources(dep).value(query->buildIndex());
            if (!mSource.isNull())
                break;
        }
    }
    mSymbol = query->query();
}

static inline List<Path> headersForSymbol(const std::shared_ptr<Project> &project, const Location &loc)
{
    List<Path> ret;
    const Path &path = loc.path();
    if (path.isHeader()) {
        ret.append(path);
        if (const DependencyNode *node = project->dependencies().value(loc.fileId())) {
            for (const auto &dependent : node->dependents) {
                const Path p = Location::path(dependent.first);
                if (p.isHeader() && dependent.second->includes.size() == 1) {
                    ret.append(p);
                    // allow headers that only include one header if we don't
                    // find anything for the real header
                }
            }
        }
    }
    return ret;
}

int IncludeFileJob::execute()
{
    if (mSource.isNull()) {
        return 1;
    }
    const Path directory = mSource.sourceFile().parentDir();
    const bool fromHeader = queryMessage()->currentFile().isHeader();
    Set<Location> last;
    int matches = 0;
    auto process = [&directory, &fromHeader, this](const Set<Location> &locations) {
        for (const Location &loc : locations) {
            bool first = true;
            for (const Path &path : headersForSymbol(project(), loc)) {
                bool found = false;
                const Symbol sym = project()->findSymbol(loc);
                if (sym.isDefinition()) {
                    switch (sym.kind) {
                    case CXCursor_FunctionDecl:
                    case CXCursor_FunctionTemplate:
                    case CXCursor_ClassDecl:
                    case CXCursor_StructDecl:
                    case CXCursor_ClassTemplate: {
                        List<String> alternatives;
                        if (path.startsWith(directory))
                            alternatives << String::format<256>("#include \"%s\"", path.mid(directory.size()).constData());
                        for (const Source::Include &inc : mSource.includePaths) {
                            const Path p = inc.path.ensureTrailingSlash();
                            if (path.startsWith(p)) {
                                alternatives << String::format<256>("#include <%s>", path.mid(p.size()).constData());
                            }
                        }
                        const int tail = strlen(path.fileName()) + 1;
                        List<String>::iterator it = alternatives.begin();
                        while (it != alternatives.end()) {
                            bool drop = false;
                            for (List<String>::const_iterator it2 = it + 1; it2 != alternatives.end(); ++it2) {
                                if (it2->size() < it->size()) {
                                    if (!strncmp(it2->constData() + 9, it->constData() + 9, it2->size() - tail - 9)) {
                                        drop = true;
                                        break;
                                    }
                                }
                            }
                            if (drop) {
                                it = alternatives.erase(it);
                            } else {
                                ++it;
                            }
                        }
                        std::sort(alternatives.begin(), alternatives.end(), [](const String &a, const String &b) {
                                return a.size() < b.size();
                            });
                        for (const auto &a : alternatives) {
                            found = true;
                            write(a);
                        }
                        break; }
                    default:
                        break;
                    }
                }
                if (first) {
                    if (found)
                        break;
                    first = false;
                }

            }
        }
    };
    project()->findSymbols(mSymbol, [&](Project::SymbolMatchType type, const String &, const Set<Location> &locations) {
            ++matches;
            if (type != Project::StartsWith) {
                process(locations);
            } else if (matches == 1) {
                last = locations;
            }
        }, queryFlags());
    if (matches == 1 && !last.isEmpty()) {
        process(last);
    }

    return 0;
}
