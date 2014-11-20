#include "console.h"

#include <QLabel>
#include <QTextBrowser>
#include <QVBoxLayout>

static Console *s_console = 0;

Console *Console::main()
{
    if (!s_console) {
        s_console = new Console("Stupido!");
    }
    return s_console;
}

Console::Console(const QString &title)
    : QWidget(0)
{
    s_console = this;
    resize(1000, 1500);

    QVBoxLayout *topLayout = new QVBoxLayout(this);

    QLabel *titleLabel = new QLabel(this);
    titleLabel->setText(title);
    QFont font = titleLabel->font();
    font.setWeight(QFont::Bold);
    titleLabel->setFont(font);
    titleLabel->setAlignment(Qt::AlignCenter);

    m_textDisplay = new QTextBrowser(this);

    topLayout->addWidget(titleLabel);
    topLayout->addWidget(m_textDisplay, 10);

    show();
}

Console::~Console()
{

}

void Console::log(const QString &message)
{
    m_textDisplay->append(message);
}
