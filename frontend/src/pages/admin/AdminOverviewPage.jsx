import { Link } from 'react-router-dom'
import { api } from '../../services/api'
import { useCatalog } from '../../context/CatalogContext'
import { useResource } from '../../hooks/useResource'
import {
  EmptyState,
  ErrorState,
  Icon,
  Loader,
  PageHeading,
  StatusBadge,
} from '../../components/common/UI'
import { date, money } from '../../utils/format'
const loadOverview = async () => {
  const [orders, users] = await Promise.all([api.getAllOrders(), api.listUsers()])
  return { orders, users }
}
export default function AdminOverviewPage() {
  const {
    products,
    loading: catalogLoading,
    error: catalogError,
    reload: reloadCatalog,
  } = useCatalog()
  const { data, loading, error, reload } = useResource(loadOverview)
  if (loading || catalogLoading) return <Loader />
  if (error || catalogError)
    return (
      <ErrorState
        message={error || catalogError}
        retry={() => {
          reload()
          reloadCatalog()
        }}
      />
    )
  const { orders, users } = data,
    low = products.filter((p) => p.stock > 0 && p.stock <= 3),
    out = products.filter((p) => p.stock <= 0)
  const statuses = orders.reduce((all, o) => ({ ...all, [o.status]: (all[o.status] || 0) + 1 }), {})
  const metrics = [
    ['Products', products.length, 'In the current collection', 'bag'],
    ['Orders', orders.length, 'Across all statuses', 'box'],
    [
      'Users',
      users.length,
      `${users.filter((u) => u.role === 'admin').length} admins · ${users.filter((u) => u.role === 'customer').length} customers`,
      'user',
    ],
    [
      'Gross order value',
      money(orders.reduce((s, o) => s + o.total, 0)),
      'All orders, including cancelled',
      'grid',
    ],
  ]
  return (
    <>
      <PageHeading eyebrow="YOUR STORE AT A GLANCE" title="A little overview.">
        The latest picture, from your store’s current records.
      </PageHeading>
      <div className="metrics">
        {metrics.map(([label, value, note, icon]) => (
          <div className="metric panel" key={label}>
            <div>
              <span>{label}</span>
              <Icon name={icon} />
            </div>
            <strong>{value}</strong>
            <p>{note}</p>
          </div>
        ))}
      </div>
      <div className="dashboard-grid">
        <section className="panel">
          <div className="section-heading">
            <h2>Recent orders</h2>
            <Link to="/admin/orders" className="text-link">
              View all ↗
            </Link>
          </div>
          {!orders.length ? (
            <EmptyState title="No orders yet">Your orders will appear here.</EmptyState>
          ) : (
            <div className="table-scroll">
              <table>
                <thead>
                  <tr>
                    <th>Order</th>
                    <th>Date</th>
                    <th>Total</th>
                    <th>Status</th>
                  </tr>
                </thead>
                <tbody>
                  {[...orders]
                    .sort((a, b) => b.createdAt - a.createdAt)
                    .slice(0, 5)
                    .map((o) => (
                      <tr key={o.id}>
                        <td>
                          <Link
                            className="inline-link"
                            to={`/admin/orders?q=${encodeURIComponent(o.id)}`}
                          >
                            #{o.id.slice(0, 8)}
                          </Link>
                        </td>
                        <td>{date(o.createdAt)}</td>
                        <td>{money(o.total)}</td>
                        <td>
                          <StatusBadge status={o.status} />
                        </td>
                      </tr>
                    ))}
                </tbody>
              </table>
            </div>
          )}
        </section>
        <section className="panel">
          <h2>Order status</h2>
          <p className="muted">Counts from all returned orders</p>
          {!orders.length ? (
            <p className="muted">No status data yet.</p>
          ) : (
            Object.entries(statuses).map(([status, count]) => (
              <div className="status-summary" key={status}>
                <div>
                  <StatusBadge status={status} />
                  <strong>{count}</strong>
                </div>
                <div className="bar-track">
                  <span style={{ width: `${(count / orders.length) * 100}%` }} />
                </div>
              </div>
            ))
          )}
        </section>
        <section className="panel">
          <div className="section-heading">
            <h2>Inventory attention</h2>
            <Link to="/admin/products" className="text-link">
              Manage ↗
            </Link>
          </div>
          <div className="inventory-counts">
            <span>
              <strong>{out.length}</strong> out of stock
            </span>
            <span>
              <strong>{low.length}</strong> low stock (1–3)
            </span>
          </div>
          {[...out, ...low].length ? (
            [...out, ...low].slice(0, 6).map((p) => (
              <div className="inventory-row" key={p.id}>
                <Link to={`/admin/products/${encodeURIComponent(p.id)}/edit`}>{p.name}</Link>
                <span className={p.stock <= 0 ? 'form-error' : 'low-stock'}>
                  {p.stock} available
                </span>
              </div>
            ))
          ) : (
            <p className="muted">No products currently need stock attention.</p>
          )}
        </section>
        <section className="panel quick-start">
          <Icon name="sun" size={32} />
          <h2>Keep the collection fresh.</h2>
          <p>Add a new find, update a product, or give your collection a little attention.</p>
          <Link className="btn primary" to="/admin/products/new">
            Add a product <Icon name="arrow" />
          </Link>
        </section>
      </div>
    </>
  )
}
