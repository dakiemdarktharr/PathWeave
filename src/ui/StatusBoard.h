#pragma once
#include "services/CareerService.h"
#include <QtWidgets>
namespace pw {
class StatusColumn : public QListWidget {
    CareerService& service_;
    QString table_, status_;

  public:
    StatusColumn(CareerService& s, QString table, QString status, QWidget* parent = nullptr)
        : QListWidget(parent), service_(s), table_(table), status_(status) {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
        setAccessibleName(displayValue(status));
    }

  protected:
    void dragEnterEvent(QDragEnterEvent* e) override {
        auto* origin = dynamic_cast<StatusColumn*>(e->source());
        if (origin && origin->table_ == table_ && &origin->service_ == &service_)
            e->acceptProposedAction();
        else
            e->ignore();
    }
    void dragMoveEvent(QDragMoveEvent* e) override {
        auto* origin = dynamic_cast<StatusColumn*>(e->source());
        if (origin && origin->table_ == table_ && &origin->service_ == &service_)
            e->acceptProposedAction();
        else
            e->ignore();
    }
    void dropEvent(QDropEvent* e) override {
        auto* origin = dynamic_cast<StatusColumn*>(e->source());
        if (!origin || origin->table_ != table_ || &origin->service_ != &service_ || origin == this ||
            !origin->currentItem()) {
            e->ignore();
            return;
        }
        auto* item = origin->currentItem();
        auto id = item->data(Qt::UserRole).toLongLong();
        try {
            service_.save(table_, {{"status", status_}}, id);
            auto* moved = origin->takeItem(origin->row(item));
            addItem(moved);
            setCurrentItem(moved);
            e->setDropAction(Qt::CopyAction);
            e->accept();
        } catch (const std::exception& error) {
            QMessageBox::warning(this, "Không chuyển được trạng thái", QString::fromUtf8(error.what()));
            e->ignore();
        }
    }
};
} // namespace pw
