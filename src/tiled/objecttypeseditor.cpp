/*
 * objecttypeseditor.cpp
 * Copyright 2016, Thorbjørn Lindeijer <bjorn@lindeijer.nl>>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "objecttypeseditor.h"
#include "ui_objecttypeseditor.h"

#include "objecttypesmodel.h"
#include "utils.h"
#include "varianteditorfactory.h"
#include "variantpropertymanager.h"

#include <QtGroupPropertyManager>

#include <QColorDialog>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QStyledItemDelegate>

namespace Tiled {
namespace Internal {

class ColorDelegate : public QStyledItemDelegate
{
public:
    ColorDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    { }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    QSize sizeHint(const QStyleOptionViewItem &,
                   const QModelIndex &) const override;
};

void ColorDelegate::paint(QPainter *painter,
                          const QStyleOptionViewItem &option,
                          const QModelIndex &index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    const QVariant displayData =
            index.model()->data(index, ObjectTypesModel::ColorRole);
    const QColor color = displayData.value<QColor>();
    const QRect rect = option.rect.adjusted(4, 4, -4, -4);

    const QPen linePen(color, 2);
    const QPen shadowPen(Qt::black, 2);

    QColor brushColor = color;
    brushColor.setAlpha(50);
    const QBrush fillBrush(brushColor);

    // Draw the shadow
    painter->setPen(shadowPen);
    painter->setBrush(QBrush());
    painter->drawRect(rect.translated(QPoint(1, 1)));

    painter->setPen(linePen);
    painter->setBrush(fillBrush);
    painter->drawRect(rect);
}

QSize ColorDelegate::sizeHint(const QStyleOptionViewItem &,
                              const QModelIndex &) const
{
    return QSize(50, 20);
}


ObjectTypesEditor::ObjectTypesEditor(QWidget *parent)
    : QMainWindow(parent, Qt::Window)
    , mUi(new Ui::ObjectTypesEditor)
    , mObjectTypesModel(new ObjectTypesModel(this))
    , mVariantManager(new VariantPropertyManager(this))
    , mGroupManager(new QtGroupPropertyManager(this))
    , mUpdating(false)
{
    mUi->setupUi(this);

    mUi->objectTypesTable->setModel(mObjectTypesModel);
    mUi->objectTypesTable->setItemDelegateForColumn(1, new ColorDelegate(this));

    QHeaderView *horizontalHeader = mUi->objectTypesTable->horizontalHeader();
    horizontalHeader->setSectionResizeMode(QHeaderView::Stretch);

    Utils::setThemeIcon(mUi->addObjectTypeButton, "add");
    Utils::setThemeIcon(mUi->removeObjectTypeButton, "remove");
    Utils::setThemeIcon(mUi->addPropertyButton, "add");
    Utils::setThemeIcon(mUi->removePropertyButton, "remove");

    mUi->propertiesView->setFactoryForManager(mVariantManager, new VariantEditorFactory(this));
    mUi->propertiesView->setResizeMode(QtTreePropertyBrowser::ResizeToContents);
    mUi->propertiesView->setRootIsDecorated(false);
    mUi->propertiesView->setPropertiesWithoutValueMarked(true);

    auto selectionModel = mUi->objectTypesTable->selectionModel();
    connect(selectionModel, &QItemSelectionModel::selectionChanged,
            this, &ObjectTypesEditor::selectedObjectTypesChanged);
    connect(selectionModel, &QItemSelectionModel::currentRowChanged,
            this, &ObjectTypesEditor::updateProperties);
    connect(mUi->objectTypesTable, SIGNAL(doubleClicked(QModelIndex)),
            SLOT(objectTypeIndexClicked(QModelIndex)));

    connect(mUi->addObjectTypeButton, SIGNAL(clicked()),
            SLOT(addObjectType()));
    connect(mUi->removeObjectTypeButton, SIGNAL(clicked()),
            SLOT(removeSelectedObjectTypes()));

    connect(mUi->addPropertyButton, SIGNAL(clicked()),
            SLOT(addProperty()));
    connect(mUi->removePropertyButton, SIGNAL(clicked()),
            SLOT(removeProperty()));
    connect(mUi->renamePropertyButton, SIGNAL(clicked()),
            SLOT(renameProperty()));

    connect(mUi->actionChooseFile, SIGNAL(triggered()),
            SLOT(chooseObjectTypesFile()));
    connect(mUi->actionImport, SIGNAL(triggered()),
            SLOT(importObjectTypes()));
    connect(mUi->actionExport, SIGNAL(triggered()),
            SLOT(exportObjectTypes()));

    connect(mObjectTypesModel, SIGNAL(dataChanged(QModelIndex,QModelIndex)),
            SLOT(applyObjectTypes()));
    connect(mObjectTypesModel, SIGNAL(rowsRemoved(QModelIndex,int,int)),
            SLOT(applyObjectTypes()));

    connect(mVariantManager, &QtVariantPropertyManager::valueChanged,
            this, &ObjectTypesEditor::propertyValueChanged);

    connect(mUi->propertiesView, &QtTreePropertyBrowser::currentItemChanged,
            this, &ObjectTypesEditor::currentItemChanged);

    Preferences *prefs = Preferences::instance();
    mObjectTypesModel->setObjectTypes(prefs->objectTypes());
}

ObjectTypesEditor::~ObjectTypesEditor()
{
    delete mUi;
}

void ObjectTypesEditor::closeEvent(QCloseEvent *event)
{
    QWidget::closeEvent(event);
    if (event->isAccepted())
        emit closed();
}

void ObjectTypesEditor::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        mUi->retranslateUi(this);
        break;
    default:
        break;
    }
}

void ObjectTypesEditor::addObjectType()
{
    const int newRow = mObjectTypesModel->objectTypes().size();
    mObjectTypesModel->appendNewObjectType();

    // Select and focus the new row and ensure it is visible
    QItemSelectionModel *sm = mUi->objectTypesTable->selectionModel();
    const QModelIndex newIndex = mObjectTypesModel->index(newRow, 0);
    sm->select(newIndex,
               QItemSelectionModel::ClearAndSelect |
               QItemSelectionModel::Rows);
    sm->setCurrentIndex(newIndex, QItemSelectionModel::Current);
    mUi->objectTypesTable->setFocus();
    mUi->objectTypesTable->scrollTo(newIndex);
}

void ObjectTypesEditor::selectedObjectTypesChanged()
{
    const QItemSelectionModel *sm = mUi->objectTypesTable->selectionModel();
    mUi->removeObjectTypeButton->setEnabled(sm->hasSelection());
}

void ObjectTypesEditor::removeSelectedObjectTypes()
{
    const QItemSelectionModel *sm = mUi->objectTypesTable->selectionModel();
    mObjectTypesModel->removeObjectTypes(sm->selectedRows());
}

void ObjectTypesEditor::objectTypeIndexClicked(const QModelIndex &index)
{
    if (index.column() == 1) {
        QColor color = mObjectTypesModel->objectTypes().at(index.row()).color;
        QColor newColor = QColorDialog::getColor(color, this);
        if (newColor.isValid())
            mObjectTypesModel->setObjectTypeColor(index.row(), newColor);
    }
}

void ObjectTypesEditor::applyObjectTypes()
{
    auto &objectTypes = mObjectTypesModel->objectTypes();

    Preferences *prefs = Preferences::instance();
    prefs->setObjectTypes(objectTypes);

    ObjectTypesWriter writer;
    if (!writer.writeObjectTypes(prefs->objectTypesFile(), objectTypes)) {
        QMessageBox::critical(this, tr("Error Writing Object Types"),
                              writer.errorString());
    }
}

void ObjectTypesEditor::applyProperties()
{
    // todo: what about multi-selection?
    auto selectionModel = mUi->objectTypesTable->selectionModel();
    auto currentIndex = selectionModel->currentIndex();
    Q_ASSERT(currentIndex.isValid());

    mObjectTypesModel->setObjectTypeProperties(currentIndex.row(), mProperties);

    applyObjectTypes();
}

void ObjectTypesEditor::chooseObjectTypesFile()
{
    Preferences *prefs = Preferences::instance();
    const QString startPath = prefs->objectTypesFile();
    // todo: change text of 'Save' button
    const QString fileName =
            QFileDialog::getSaveFileName(this, tr("Choose Object Types File"),
                                         startPath,
                                         tr("Object Types files (*.xml)"),
                                         nullptr,
                                         QFileDialog::DontConfirmOverwrite);

    if (fileName.isEmpty())
        return;

    prefs->setLastPath(Preferences::ObjectTypesFile, fileName);

    ObjectTypes objectTypes;

    if (QFile::exists(fileName)) {
        ObjectTypesReader reader;
        objectTypes = reader.readObjectTypes(fileName);

        if (!reader.errorString().isEmpty()) {
            QMessageBox::critical(this, tr("Error Reading Object Types"),
                                  reader.errorString());
            return;
        }
    }

    prefs->setObjectTypesFile(fileName);
    prefs->setObjectTypes(objectTypes);
    mObjectTypesModel->setObjectTypes(objectTypes);
}

void ObjectTypesEditor::importObjectTypes()
{
    Preferences *prefs = Preferences::instance();
    const QString lastPath = prefs->lastPath(Preferences::ObjectTypesFile);
    const QString fileName =
            QFileDialog::getOpenFileName(this, tr("Import Object Types"),
                                         lastPath,
                                         tr("Object Types files (*.xml)"));
    if (fileName.isEmpty())
        return;

    prefs->setLastPath(Preferences::ObjectTypesFile, fileName);

    ObjectTypesReader reader;
    ObjectTypes objectTypes = reader.readObjectTypes(fileName);
    // todo: merge with existing types?

    if (reader.errorString().isEmpty()) {
        prefs->setObjectTypes(objectTypes);
        mObjectTypesModel->setObjectTypes(objectTypes);
    } else {
        QMessageBox::critical(this, tr("Error Reading Object Types"),
                              reader.errorString());
    }
}

void ObjectTypesEditor::exportObjectTypes()
{
    Preferences *prefs = Preferences::instance();
    QString lastPath = prefs->lastPath(Preferences::ObjectTypesFile);

    if (!lastPath.endsWith(QLatin1String(".xml")))
        lastPath.append(QLatin1String("/objecttypes.xml"));

    const QString fileName =
            QFileDialog::getSaveFileName(this, tr("Export Object Types"),
                                         lastPath,
                                         tr("Object Types files (*.xml)"));
    if (fileName.isEmpty())
        return;

    prefs->setLastPath(Preferences::ObjectTypesFile, fileName);

    ObjectTypesWriter writer;
    if (!writer.writeObjectTypes(fileName, prefs->objectTypes())) {
        QMessageBox::critical(this, tr("Error Writing Object Types"),
                              writer.errorString());
    }
}

void ObjectTypesEditor::updateProperties()
{
    auto selectionModel = mUi->objectTypesTable->selectionModel();
    auto currentIndex = selectionModel->currentIndex();
    ObjectType objectType = mObjectTypesModel->objectTypeAt(currentIndex);

    // todo: what to do with multiple selected objects?
    mProperties = objectType.defaultProperties;

    mUpdating = true;
    mVariantManager->clear();
    mNameToProperty.clear();

    QMapIterator<QString,QString> it(mProperties);
    while (it.hasNext()) {
        it.next();
        QtVariantProperty *property = createProperty(QVariant::String,
                                                     it.key());
        property->setValue(it.value());
    }
    mUpdating = false;
}

void ObjectTypesEditor::propertyValueChanged(QtProperty *property,
                                             const QVariant &val)
{
    if (mUpdating)
        return;

    mProperties.insert(property->propertyName(), val.toString());
    applyProperties();
}

QtVariantProperty *ObjectTypesEditor::createProperty(int type,
                                                     const QString &name)
{
    QtVariantProperty *property = mVariantManager->addProperty(type, name);
    if (type == QVariant::Bool)
        property->setAttribute(QLatin1String("textVisible"), false);

    mUi->propertiesView->addProperty(property);
    mNameToProperty.insert(name, property);

    return property;
}

void ObjectTypesEditor::addProperty()
{
    QInputDialog *dialog = new QInputDialog(window());
    dialog->setInputMode(QInputDialog::TextInput);
    dialog->setLabelText(tr("Name:"));
    dialog->setWindowTitle(tr("Add Property"));
    dialog->open(this, SLOT(addProperty(QString)));
}

void ObjectTypesEditor::addProperty(const QString &name, const QString &value)
{
    if (name.isEmpty())
        return;

    if (!mProperties.contains(name)) {
        mProperties.insert(name, value);

        // Determine the property preceding the new property, if any
        const int index = mProperties.keys().indexOf(name);
        const QList<QtProperty *> properties = mUi->propertiesView->properties();
        QtProperty *precedingProperty = (index > 0) ? properties.at(index - 1) : nullptr;

        mUpdating = true;
        QtVariantProperty *property = mVariantManager->addProperty(QVariant::String, name);
        property->setValue(value);
        mUi->propertiesView->insertProperty(property, precedingProperty);
        mNameToProperty.insert(name, property);
        mUpdating = false;

        applyProperties();
    }

    editCustomProperty(name);
}

void ObjectTypesEditor::editCustomProperty(const QString &name)
{
    QtVariantProperty *property = mNameToProperty.value(name);
    if (!property)
        return;

    const QList<QtBrowserItem*> propertyItems = mUi->propertiesView->items(property);
    if (!propertyItems.isEmpty())
        mUi->propertiesView->editItem(propertyItems.first());
}

void ObjectTypesEditor::removeProperty()
{
    QtBrowserItem *item = mUi->propertiesView->currentItem();
    if (!item)
        return;

    const QString name = item->property()->propertyName();
    QList<QtBrowserItem *> items = mUi->propertiesView->topLevelItems();
    if (items.count() > 1) {
        int currentItemIndex = items.indexOf(item);
        if (item == items.last()) {
            mUi->propertiesView->setCurrentItem(items.at(currentItemIndex - 1));
        } else {
            mUi->propertiesView->setCurrentItem(items.at(currentItemIndex + 1));
        }
    }

    mProperties.remove(name);
    delete mNameToProperty.take(name);

    applyProperties();
}

void ObjectTypesEditor::renameProperty()
{
    QtBrowserItem *item = mUi->propertiesView->currentItem();
    if (!item)
        return;

    const QString oldName = item->property()->propertyName();

    QInputDialog *dialog = new QInputDialog(mUi->propertiesView);
    dialog->setInputMode(QInputDialog::TextInput);
    dialog->setLabelText(tr("Name:"));
    dialog->setTextValue(oldName);
    dialog->setWindowTitle(tr("Rename Property"));
    dialog->open(this, SLOT(renameProperty(QString)));
}

void ObjectTypesEditor::renameProperty(const QString &name)
{
    if (name.isEmpty())
        return;

    QtBrowserItem *item = mUi->propertiesView->currentItem();
    if (!item)
        return;

    const QString oldName = item->property()->propertyName();
    if (oldName == name)
        return;

    QString value = mProperties.take(oldName);
    delete mNameToProperty.take(oldName);
    addProperty(name, value);
}

void ObjectTypesEditor::currentItemChanged(QtBrowserItem *item)
{
    bool itemSelected = item != nullptr;
    mUi->removePropertyButton->setEnabled(itemSelected);
    mUi->renamePropertyButton->setEnabled(itemSelected);
}

} // namespace Internal
} // namespace Tiled
