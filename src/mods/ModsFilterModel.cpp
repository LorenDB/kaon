#include "ModsFilterModel.h"

class ModsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    static ModsModel *instance()
    {
        static ModsModel m;
        return &m;
    }

    void registerMod(Mod *mod)
    {
        if (mod)
        {
            beginInsertRows({}, m_mods.size(), m_mods.size());
            m_mods.push_back(mod);
            endInsertRows();
        }
    }

    enum Roles
    {
        ModObject = Qt::UserRole + 1,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const final { return m_mods.size(); }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const final
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_mods.size())
            return {};
        const auto &item = m_mods.at(index.row());
        switch (role)
        {
        case Qt::DisplayRole:
            return item->displayName();
        case Roles::ModObject:
            return QVariant::fromValue(item);
        }

        return {};
    }

    QHash<int, QByteArray> roleNames() const final { return {{Roles::ModObject, "mod"_ba}}; }

private:
    explicit ModsModel()
        : QAbstractListModel{}
    {}
    ~ModsModel() {}

    QList<Mod *> m_mods;
};

ModsFilterModel::ModsFilterModel(QObject *parent)
    : QSortFilterProxyModel{parent}
{
    connect(this, &ModsFilterModel::gameChanged, this, &ModsFilterModel::invalidateFilter);
    connect(this, &ModsFilterModel::searchChanged, this, &ModsFilterModel::invalidateFilter);
    connect(this, &ModsFilterModel::showIncompatibleChanged, this, &ModsFilterModel::invalidateFilter);

    setSourceModel(ModsModel::instance());

    setDynamicSortFilter(true);
    sort(0);
}

void ModsFilterModel::registerMod(Mod *mod)
{
    ModsModel::instance()->registerMod(mod);
}

void ModsFilterModel::setGame(Game *game)
{
    m_game = game;
    emit gameChanged(game);
}

void ModsFilterModel::setSearch(const QString &search)
{
    m_search = search;
    emit searchChanged();
}

bool ModsFilterModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const auto mod = sourceModel()->data(sourceModel()->index(row, 0, parent), ModsModel::Roles::ModObject).value<Mod *>();
    if (!mod)
        return false;
    if (!m_search.isEmpty() && !mod->displayName().contains(m_search, Qt::CaseInsensitive))
        return false;
    if (m_game && m_game->isValid())
    {
        if (!m_showIncompatible && !mod->checkGameCompatibility(m_game))
            return false;
    }
    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}

bool ModsFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const auto leftMod = sourceModel()->data(left, ModsModel::Roles::ModObject).value<Mod *>();
    const auto rightMod = sourceModel()->data(right, ModsModel::Roles::ModObject).value<Mod *>();

    if (m_game)
    {
        const auto leftCompat = leftMod->checkGameCompatibility(m_game);
        const auto rightCompat = rightMod->checkGameCompatibility(m_game);
        if (leftCompat != rightCompat)
            return leftCompat > rightCompat;
    }

    return leftMod->displayName() < rightMod->displayName();
}

#include "ModsFilterModel.moc"
