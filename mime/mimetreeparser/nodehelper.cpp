/*
  Copyright (C) 2009 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.net
  Copyright (c) 2009 Andras Mantia <andras@kdab.net>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include "nodehelper.h"
#include "mimetreeparser_debug.h"

#include <KMime/Content>

namespace MimeTreeParser
{

void NodeHelper::setNodeProcessed(KMime::Content *node, bool recurse)
{
    if (!node) {
        return;
    }
    mProcessedNodes.append(node);
    // qCDebug(MIMETREEPARSER_LOG) << "Node processed: " << node->index().toString() << node->contentType()->as7BitString();
    //<< " decodedContent" << node->decodedContent();
    if (recurse) {
        const auto contents = node->contents();
        for (KMime::Content *c : contents) {
            setNodeProcessed(c, true);
        }
    }
}

void NodeHelper::setNodeUnprocessed(KMime::Content *node, bool recurse)
{
    if (!node) {
        return;
    }
    mProcessedNodes.removeAll(node);

    // qCDebug(MIMETREEPARSER_LOG) << "Node UNprocessed: " << node;
    if (recurse) {
        const auto contents = node->contents();
        for (KMime::Content *c : contents) {
            setNodeUnprocessed(c, true);
        }
    }
}

bool NodeHelper::nodeProcessed(KMime::Content *node) const
{
    if (!node) {
        return true;
    }
    return mProcessedNodes.contains(node);
}

}
