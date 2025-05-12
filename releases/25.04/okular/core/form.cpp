/*
    SPDX-FileCopyrightText: 2007 Pino Toscano <pino@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "form.h"
#include "form_p.h"

// qt includes
#include <QVariant>

#include "action.h"

using namespace Okular;

FormFieldPrivate::FormFieldPrivate(FormField::FieldType type)
    : m_type(type)
    , m_activateAction(nullptr)
    , q_ptr(nullptr)
{
}

FormFieldPrivate::~FormFieldPrivate()
{
    delete m_activateAction;
    qDeleteAll(m_additionalActions);
    qDeleteAll(m_additionalAnnotActions);
}

void FormFieldPrivate::setDefault()
{
    m_default = value();
}

FormField::FormField(FormFieldPrivate &dd)
    : d_ptr(&dd)
{
    d_ptr->q_ptr = this;
}

FormField::~FormField()
{
    delete d_ptr;
}

FormField::FieldType FormField::type() const
{
    Q_D(const FormField);
    return d->m_type;
}

bool FormField::isReadOnly() const
{
    return false;
}

void FormField::setReadOnly(bool)
{
}

bool FormField::isVisible() const
{
    return true;
}

void FormField::setVisible(bool)
{
}

bool FormField::isPrintable() const
{
    return true;
}

void FormField::setPrintable(bool)
{
}

void FormField::setValue(const QVariant &)
{
}

void FormField::setAppearanceValue(const QVariant &)
{
}

QVariant FormField::value() const
{
    return QVariant(QString());
}

Action *FormField::activationAction() const
{
    Q_D(const FormField);
    return d->m_activateAction;
}

void FormField::setActivationAction(Action *action)
{
    Q_D(FormField);
    delete d->m_activateAction;
    d->m_activateAction = action;
}

Action *FormField::additionalAction(AdditionalActionType type) const
{
    Q_D(const FormField);
    return d->m_additionalActions.value(type);
}

void FormField::setAdditionalAction(AdditionalActionType type, Action *action)
{
    Q_D(FormField);
    delete d->m_additionalActions.value(type);
    d->m_additionalActions[type] = action;
}

Action *FormField::additionalAction(Annotation::AdditionalActionType type) const
{
    Q_D(const FormField);
    return d->m_additionalAnnotActions.value(type);
}

void FormField::setAdditionalAction(Annotation::AdditionalActionType type, Action *action)
{
    Q_D(FormField);
    delete d->m_additionalAnnotActions.value(type);
    d->m_additionalAnnotActions[type] = action;
}

QList<Action *> FormField::additionalActions() const
{
    Q_D(const FormField);
    // yes, calling values() is not great but it's a list of ~10 elements, we can live with that
    return d->m_additionalAnnotActions.values() + d->m_additionalActions.values(); // clazy:exclude=container-anti-pattern
}

Page *FormField::page() const
{
    Q_D(const FormField);
    return d->m_page;
}

QString FormField::committedValue() const
{
    Q_D(const FormField);
    return d->m_committedValue;
}

void FormField::commitValue(const QString &value)
{
    Q_D(FormField);
    d->m_committedValue = value;
}

QString FormField::committedFormattedValue() const
{
    Q_D(const FormField);
    return d->m_committedFormattedValue;
}

void FormField::commitFormattedValue(const QString &value)
{
    Q_D(FormField);
    d->m_committedFormattedValue = value;
}

class Okular::FormFieldButtonPrivate : public Okular::FormFieldPrivate
{
public:
    FormFieldButtonPrivate()
        : FormFieldPrivate(FormField::FormButton)
    {
    }

    Q_DECLARE_PUBLIC(FormFieldButton)

    void setValue(const QString &v) override
    {
        Q_Q(FormFieldButton);
        q->setState(QVariant(v).toBool());
    }

    QString value() const override
    {
        Q_Q(const FormFieldButton);
        return QVariant::fromValue<bool>(q->state()).toString();
    }
};

FormFieldButton::FormFieldButton()
    : FormField(*new FormFieldButtonPrivate())
{
}

FormFieldButton::~FormFieldButton()
{
}

void FormFieldButton::setState(bool)
{
}

void FormFieldButton::setIcon(Okular::FormField *)
{
}

class Okular::FormFieldTextPrivate : public Okular::FormFieldPrivate
{
public:
    FormFieldTextPrivate()
        : FormFieldPrivate(FormField::FormText)
    {
    }

    Q_DECLARE_PUBLIC(FormFieldText)

    void setValue(const QString &v) override
    {
        Q_Q(FormFieldText);
        q->setText(v);
    }

    QString value() const override
    {
        Q_Q(const FormFieldText);
        return q->text();
    }
};

FormFieldText::FormFieldText()
    : FormField(*new FormFieldTextPrivate())
{
}

FormFieldText::~FormFieldText()
{
}

void FormFieldText::setText(const QString &)
{
}

bool FormFieldText::isPassword() const
{
    return false;
}

bool FormFieldText::isRichText() const
{
    return false;
}

int FormFieldText::maximumLength() const
{
    return -1;
}

Qt::Alignment FormFieldText::textAlignment() const
{
    return Qt::AlignVCenter | Qt::AlignLeft;
}

bool FormFieldText::canBeSpellChecked() const
{
    return false;
}

void FormFieldText::setValue(const QVariant &value)
{
    setText(value.toString());
}

void FormFieldText::setAppearanceValue(const QVariant &value)
{
    setAppearanceText(value.toString());
}

QVariant FormFieldText::value() const
{
    return QVariant(text());
}

class Okular::FormFieldChoicePrivate : public Okular::FormFieldPrivate
{
public:
    FormFieldChoicePrivate()
        : FormFieldPrivate(FormField::FormChoice)
    {
    }

    Q_DECLARE_PUBLIC(FormFieldChoice)

    void setValue(const QString &v) override
    {
        Q_Q(FormFieldChoice);
        const QStringList choices = v.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        QList<int> newchoices;
        for (const QString &str : choices) {
            bool ok = true;
            int val = str.toInt(&ok);
            if (ok) {
                newchoices.append(val);
            }
        }
        if (!newchoices.isEmpty()) {
            q->setCurrentChoices(newchoices);
        }
    }

    QString value() const override
    {
        Q_Q(const FormFieldChoice);
        QList<int> choices = q->currentChoices();
        std::sort(choices.begin(), choices.end());
        QStringList list;
        for (const int c : std::as_const(choices)) {
            list.append(QString::number(c));
        }
        return list.join(QStringLiteral(";"));
    }

    QMap<QString, QString> exportValues;
};

FormFieldChoice::FormFieldChoice()
    : FormField(*new FormFieldChoicePrivate())
{
}

FormFieldChoice::~FormFieldChoice()
{
}

bool FormFieldChoice::isEditable() const
{
    return false;
}

bool FormFieldChoice::multiSelect() const
{
    return false;
}

void FormFieldChoice::setCurrentChoices(const QList<int> &)
{
}

QString FormFieldChoice::editChoice() const
{
    return QString();
}

void FormFieldChoice::setEditChoice(const QString &)
{
}

Qt::Alignment FormFieldChoice::textAlignment() const
{
    return Qt::AlignVCenter | Qt::AlignLeft;
}

bool FormFieldChoice::canBeSpellChecked() const
{
    return false;
}

void FormFieldChoice::setExportValues(const QMap<QString, QString> &values)
{
    Q_D(FormFieldChoice);
    d->exportValues = values;
}

QString FormFieldChoice::exportValueForChoice(const QString &choice) const
{
    Q_D(const FormFieldChoice);
    return d->exportValues.value(choice, choice);
}

void FormFieldChoice::setValue(const QVariant &value)
{
    if (choiceType() == ComboBox) {
        const QStringList availableChoices = choices();
        const QString valueToSet = value.toString();
        for (int i = 0; i < availableChoices.size(); i++) {
            if (valueToSet == availableChoices[i]) {
                const QList oneChoiceList = {i};
                setCurrentChoices(oneChoiceList);
                return; // We expect to set only one element for a combo box
            }
        }
        // If the value to be set is not present in the available choices
        setEditChoice(valueToSet);
    } else {
        // TODO what to set for ListBox.
    }
}

QVariant FormFieldChoice::value() const
{
    if (choiceType() == ComboBox) {
        if (currentChoices().isEmpty()) {
            return QVariant(editChoice());
        }
        return QVariant(choices().at(currentChoices().constFirst()));
    } else {
        // TODO what to return for ListBox. Temporarily return empty string.
        return QVariant(QString());
    }
}

void FormFieldChoice::setAppearanceValue(const QVariant &value)
{
    if (choiceType() == ComboBox) {
        setAppearanceChoiceText(value.toString());
    }
}

class Okular::FormFieldSignaturePrivate : public Okular::FormFieldPrivate
{
public:
    FormFieldSignaturePrivate()
        : FormFieldPrivate(FormField::FormSignature)
    {
    }

    Q_DECLARE_PUBLIC(FormFieldSignature)

    void setValue(const QString &v) override
    {
        Q_UNUSED(v)
    }

    QString value() const override
    {
        return QString();
    }
};

FormFieldSignature::FormFieldSignature()
    : FormField(*new FormFieldSignaturePrivate())
{
}

FormFieldSignature::~FormFieldSignature()
{
}
