/*  YUView - YUV player with advanced analytics toolset
*   Copyright (C) 2015  Institut für Nachrichtentechnik
*                       RWTH Aachen University, GERMANY
*
*   YUView is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   YUView is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with YUView.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "playlistItemContainer.h"

#include <algorithm>
#include <QPainter>
#include "statisticHandler.h"

playlistItemContainer::playlistItemContainer(const QString &itemNameOrFileName) : playlistItem(itemNameOrFileName, playlistItem_Indexed)
{
  // By default, there is no limit on the number of items
  maxItemCount = -1;
  // No update required (yet)
  childLlistUpdateRequired = true;
  // By default, take the maximum limits for all items
  frameLimitsMax = true;

  // Enable dropping for container items
  setFlags(flags() | Qt::ItemIsDropEnabled);

  containerStatLayout.setContentsMargins(0, 0, 0, 0);
}

// If the maximum number of items is reached, return false.
bool playlistItemContainer::acceptDrops(playlistItem *draggingItem) const
{
  Q_UNUSED(draggingItem);
  return (maxItemCount == -1 || childList.count() < maxItemCount);
}

indexRange playlistItemContainer::getStartEndFrameLimits() const
{
  indexRange limits(-1, -1);

  // Go through all items
  for (playlistItem *item : childList)
  {
    if (item->isIndexedByFrame())
    {
      indexRange limit = item->getStartEndFrameLimits();

      if (limits == indexRange(-1, -1))
        limits = limit;

      if (frameLimitsMax)
      {
        // As much as any of the items allows
        limits.first = std::min(limits.first, limit.first);
        limits.second = std::max(limits.second, limit.second);
      }
      else
      {
        // Only "overlapping" range
        limits.first = std::max(limits.first, limit.first);
        limits.second = std::min(limits.second, limit.second);
      }
    }
  }
  
  return limits;
}

void playlistItemContainer::drawEmptyContainerText(QPainter *painter, double zoomFactor)
{
  // Draw an error text in the view instead of showing an empty image
  // Get the size of the text and create a QRect of that size which is centered at (0,0)
  QFont displayFont = painter->font();
  displayFont.setPointSizeF(painter->font().pointSizeF() * zoomFactor);
  painter->setFont(displayFont);
  QSize textSize = painter->fontMetrics().size(0, emptyText);
  QRect textRect;
  textRect.setSize(textSize);
  textRect.moveCenter(QPoint(0,0));

  // Draw the text
  painter->drawText(textRect, emptyText);
}

void playlistItemContainer::updateChildList()
{
  // Disconnect all signalItemChanged event from the children
  for (int i = 0; i < childList.count(); i++)
  {
    disconnect(childList[i], &playlistItem::signalItemChanged, nullptr, nullptr);
    disconnect(childList[i], &playlistItem::signalItemCacheCleared, nullptr, nullptr);
    if (childList[i]->providesStatistics())
      childList[i]->getStatisticsHandler()->deleteSecondaryStatisticsHandlerControls();
  }

  // Connect all child items
  childList.clear();
  for (int i = 0; i < childCount(); i++)
  {
    playlistItem *childItem = dynamic_cast<playlistItem*>(child(i));
    if (childItem)
    {
      connect(childItem, &playlistItem::signalItemChanged, this, &playlistItemContainer::childChanged);
      connect(childItem, &playlistItem::signalItemCacheCleared, this, &playlistItem::signalItemCacheCleared);
      childList.append(childItem);
    }
  }

  // Remove all widgets (the lines and spacer) that are still in the layout
  QLayoutItem *child;
  while ((child = containerStatLayout.takeAt(0)) != 0) 
    delete child;

  // Now add the statistics controls from all items that can provide statistics
  bool statisticsPresent = false;
  for (int i = 0; i < childList.count(); i++)
    if (childList[i]->providesStatistics())
    {
      // Add a line and the statistics controls also to the overlay widget
      QFrame *line = new QFrame;
      line->setObjectName(QStringLiteral("line"));
      line->setFrameShape(QFrame::HLine);
      line->setFrameShadow(QFrame::Sunken);

      containerStatLayout.addWidget(line);
      containerStatLayout.addWidget(childList[i]->getStatisticsHandler()->getSecondaryStatisticsHandlerControls());
      statisticsPresent = true;
    }

  if (statisticsPresent)
    // Add a spacer item at the end
    containerStatLayout.addSpacerItem(new QSpacerItem(0, 10, QSizePolicy::Ignored, QSizePolicy::MinimumExpanding));

  // Finally, we have to update the start/end Frame
  childChanged(false);
  emit signalItemChanged(true);

  childLlistUpdateRequired = false;
}

void playlistItemContainer::itemAboutToBeDeleted(playlistItem *item)
{
  // Remove the item from childList and disconnect signals/slots
  for (int i = 0; i < childList.count(); i++)
  {
    if (childList[i] == item)
    {
      disconnect(childList[i], &playlistItem::signalItemChanged, nullptr, nullptr);
      disconnect(childList[i], &playlistItem::signalItemCacheCleared, nullptr, nullptr);
      if (childList[i]->providesStatistics())
        childList[i]->getStatisticsHandler()->deleteSecondaryStatisticsHandlerControls();
      childList.removeAt(i);
    }
  }
}

void playlistItemContainer::childChanged(bool redraw)
{
  // Update the index range 
  startEndFrame = indexRange(-1,-1);
  for (int i = 0; i < childList.count(); i++)
  {
    if (childList[i]->isIndexedByFrame())
    {
      indexRange itemRange = childList[i]->getFrameIndexRange();
      if (startEndFrame == indexRange(-1, -1))
        startEndFrame = itemRange;

      if (frameLimitsMax)
      {
        // As much as any of the items allows
        startEndFrame.first = std::min(startEndFrame.first, itemRange.first);
        startEndFrame.second = std::max(startEndFrame.second, itemRange.second);
      }
      else
      {
        // Only "overlapping" range
        startEndFrame.first = std::max(startEndFrame.first, itemRange.first);
        startEndFrame.second = std::min(startEndFrame.second, itemRange.second);
      }
    }
  }

  if (redraw)
    // A child item changed and it needs redrawing, so we need to re-layout everything and also redraw
    emit signalItemChanged(true);
}

bool playlistItemContainer::isSourceChanged()
{
  // Check the children. Always call isSourceChanged() on all children because this function
  // also resets the flag.
  bool changed = false;
  for (int i = 0; i < childList.count(); i++)
  {
    playlistItem *childItem = dynamic_cast<playlistItem*>(child(i));
    if (childItem->isSourceChanged())
      changed = true;
  }

  return changed;
}

void playlistItemContainer::reloadItemSource()
{
  for (int i = 0; i < childList.count(); i++)
  {
    playlistItem *childItem = dynamic_cast<playlistItem*>(child(i));
    childItem->reloadItemSource();
  }
}

void playlistItemContainer::updateFileWatchSetting()
{
  for (int i = 0; i < childList.count(); i++)
  {
    playlistItem *childItem = dynamic_cast<playlistItem*>(child(i));
    childItem->updateFileWatchSetting();
  }
}

QSize playlistItemContainer::getSize() const
{ 
  // Return the size of the text that is drawn on screen.
  QPainter painter;
  QFont displayFont = painter.font();
  return painter.fontMetrics().size(0, emptyText);
}

playlistItem *playlistItemContainer::getFirstChildPlaylistItem() const
{
  if (childList.count() == 0)
    return nullptr;

  return childList.at(0);
}

void playlistItemContainer::savePlaylistChildren(QDomElement &root, const QDir &playlistDir) const
{
  // Append all children
  for (playlistItem *item : childList)
    item->savePlaylist(root, playlistDir);
}
