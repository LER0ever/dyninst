/*
 * Copyright (c) 1996-2007 Barton P. Miller
 * 
 * We provide the Paradyn Parallel Performance Tools (below
 * described as "Paradyn") on an AS IS basis, and do not warrant its
 * validity or performance.  We reserve the right to update, modify,
 * or discontinue this software at any time.  We shall have no
 * obligation to supply such updates or modifications or any other
 * form of support to you.
 * 
 * By your use of Paradyn, you understand and agree that we (or any
 * other person or entity with proprietary rights in Paradyn) are
 * under no obligation to provide either maintenance services,
 * update services, notices of latent defects, or correction of
 * defects for Paradyn.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <algorithm>

#include "common/h/Timer.h"
#include "common/h/debugOstream.h"
#include "common/h/serialize.h"
#include "common/h/pathName.h"

#include "Serialization.h"
#include "Symtab.h"
#include "Symbol.h"
#include "Module.h"
#include "Collections.h"
#include "Function.h"
#include "Variable.h"

#include "symtabAPI/src/Object.h"

using namespace Dyninst;
using namespace Dyninst::SymtabAPI;
using namespace std;

/*
 * We're changing the type of a symbol. Therefore we need to rip it out of the indices
 * for whatever it used to be (also, aggregations) and put it in the new ones. 
 * Oy. 
 */

bool Symtab::changeType(Symbol *sym, Symbol::SymbolType oldType)
{
    switch (oldType) {
    case Symbol::ST_FUNCTION: {
        Function *func = NULL;
        if (findFuncByEntryOffset(func, sym->getAddr())) {
            // Remove this symbol from the function
            func->removeSymbol(sym);
            // What if we removed the last symbol from the function?
            // Argh. Ah, well. Users may do that - leave it there for now.
        break;
        }
    }
    case Symbol::ST_OBJECT: {
        Variable *var = NULL;
        if (findVariableByOffset(var, sym->getAddr())) {
            var->removeSymbol(sym);
            // See above
        }
        break;
    }
    case Symbol::ST_MODULE: {
        // TODO Module should be an Aggregation
        break;
    }
    default:
        break;
    }

    addSymbolToIndices(sym);
    addSymbolToAggregates(sym);

    return true;
}

bool Symtab::delSymbol(Symbol *sym)
{
    switch (sym->getType()) {
    case Symbol::ST_FUNCTION: {
        Function *func = NULL;
        if (findFuncByEntryOffset(func, sym->getAddr())) {
            // Remove this symbol from the function
            func->removeSymbol(sym);
            // What if we removed the last symbol from the function?
            // Argh. Ah, well. Users may do that - leave it there for now.
        break;
        }
    }
    case Symbol::ST_OBJECT: {
        Variable *var = NULL;
        if (findVariableByOffset(var, sym->getAddr())) {
            var->removeSymbol(sym);
            // See above
        }
        break;
    }
    case Symbol::ST_MODULE: {
        // TODO Module should be an Aggregation
        break;
    }
    default:
        break;
    }

    // Remove from global indices
    for (std::vector<Symbol *>::iterator iter = everyDefinedSymbol.begin();
         iter != everyDefinedSymbol.end();
         iter++) {
        if (*iter == sym) {
            everyDefinedSymbol.erase(iter);
            break;
        }
    }
    // Delete from user added symbols
    for (std::vector<Symbol *>::iterator iter = userAddedSymbols.begin();
         iter != userAddedSymbols.end();
         iter++) {
        if (*iter == sym) {
            userAddedSymbols.erase(iter);
            break;
        }
    }
    // From undefined dynamic symbols, if it exists
    if (undefDynSyms.find(sym->getName()) != undefDynSyms.end())
        undefDynSyms.erase(sym->getName());

    // And now from the other maps
    // symsByOffset:
    std::vector<Symbol *> offsetSyms = symsByOffset[sym->getAddr()];
    for (std::vector<Symbol *>::iterator iter = offsetSyms.begin();
         iter != offsetSyms.end();
         iter++) {
        if (*iter == sym) {
            offsetSyms.erase(iter);
        }
    }
    symsByOffset[sym->getAddr()] = offsetSyms;

    // symsByMangledName, same idea. 
    std::vector<Symbol *> mangledSyms = symsByMangledName[sym->getName()];
    for (std::vector<Symbol *>::iterator iter = mangledSyms.begin();
         iter != mangledSyms.end();
         iter++) {
        if (*iter == sym) {
            mangledSyms.erase(iter);
        }
    }
    symsByMangledName[sym->getName()] = mangledSyms;

    // symsByPrettyName, same idea. 
    std::vector<Symbol *> prettySyms = symsByPrettyName[sym->getPrettyName()];
    for (std::vector<Symbol *>::iterator iter = prettySyms.begin();
         iter != prettySyms.end();
         iter++) {
        if (*iter == sym) {
            prettySyms.erase(iter);
        }
    }
    symsByPrettyName[sym->getPrettyName()] = prettySyms;

    // symsByTypedName, same idea. 
    std::vector<Symbol *> typedSyms = symsByTypedName[sym->getTypedName()];
    for (std::vector<Symbol *>::iterator iter = typedSyms.begin();
         iter != typedSyms.end();
         iter++) {
        if (*iter == sym) {
            typedSyms.erase(iter);
        }
    }
    symsByTypedName[sym->getTypedName()] = typedSyms;

    // Don't delete; it still exists in the linkedFile
    return true;
}

bool Symtab::addSymbol(Symbol *newSym, Symbol *referringSymbol) 
{
    if (!newSym)
    	return false;

    string filename = referringSymbol->getModule()->exec()->name();
    vector<string> *vers, *newSymVers = new vector<string>;
    newSym->setVersionFileName(filename);
    std::string rstr;

    bool ret = newSym->getVersionFileName(rstr);
    if (!ret) 
    {
       fprintf(stderr, "%s[%d]:  failed to getVersionFileName(%s)\n", 
             FILE__, __LINE__, rstr.c_str());
    }

    if (referringSymbol->getVersions(vers) && vers != NULL && vers->size() > 0) 
    {
        newSymVers->push_back((*vers)[0]);
        newSym->setVersions(*newSymVers);
    }

    return addSymbol(newSym, true);
}

bool Symtab::addSymbol(Symbol *newSym, bool isDynamic) 
{
    if (!newSym)
    	return false;

    if (isDynamic) 
    {
        newSym->clearIsInSymtab();
        newSym->setDynSymtab();
    }	

#if !defined(os_windows)
    // Windows: variables are created with an empty module
    if (newSym->getModuleName().length() == 0) 
    {
        //fprintf(stderr, "SKIPPING EMPTY MODULE\n");
        return false;
    }
#endif
    
    // This mimics the behavior during parsing

    fixSymModule(newSym);

    // If there aren't any pretty names, create them
    if (newSym->getPrettyName() == "") {
        demangleSymbol(newSym);
    }

    // Add to appropriate indices
    addSymbolToIndices(newSym);

    // And to aggregates
    addSymbolToAggregates(newSym);

    // And to "new symbols added by user"
    userAddedSymbols.push_back(newSym);

    return true;
}
