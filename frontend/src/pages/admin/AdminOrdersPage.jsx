import { Fragment, useRef, useState } from 'react'
import { Link, useSearchParams } from 'react-router-dom'
import { api, ORDER_STATUSES, orderTransitions } from '../../services/api'
import { useResource } from '../../hooks/useResource'
import { useToast } from '../../context/ToastContext'
import {
  Button,
  EmptyState,
  ErrorState,
  Loader,
  Modal,
  PageHeading,
  StatusBadge,
} from '../../components/common/UI'
import OrderDetails from '../../components/orders/OrderDetails'
import { date, money } from '../../utils/format'
export default function AdminOrdersPage() {
  const { data, loading, error, reload } = useResource(api.getAllOrders),
    notify = useToast(),
    [params] = useSearchParams()
  const [query, setQuery] = useState(params.get('q') || ''),
    [status, setStatus] = useState(''),
    [sort, setSort] = useState('newest'),
    [expanded, setExpanded] = useState(null),
    [change, setChange] = useState(null),
    [busy, setBusy] = useState(false),
    [failure, setFailure] = useState(''),
    lock = useRef(false)
  const orders = data || [],
    statuses = [...new Set([...ORDER_STATUSES, ...orders.map((o) => o.status)])]
  const filtered = orders
    .filter(
      (o) =>
        `${o.id} ${o.userId} ${o.items.map((i) => i.name).join(' ')}`
          .toLowerCase()
          .includes(query.toLowerCase()) &&
        (!status || o.status === status),
    )
    .sort((a, b) =>
      sort === 'oldest'
        ? a.createdAt - b.createdAt
        : sort === 'total'
          ? b.total - a.total
          : b.createdAt - a.createdAt,
    )
  const update = async () => {
    if (lock.current) return
    lock.current = true
    setBusy(true)
    setFailure('')
    try {
      await api.updateOrderStatus(change.order.id, change.status, change.order.status)
      setChange(null)
      notify('Order status updated.')
      await reload()
    } catch (e) {
      setFailure(e.message)
    } finally {
      lock.current = false
      setBusy(false)
    }
  }
  return (
    <>
      <PageHeading eyebrow="ORDER MANAGEMENT" title="Orders">
        Review order details and maintain their status.
      </PageHeading>
      <div className="notice">
        <p>
          Orders progress from pending to paid, shipped and delivered. Cancellation before shipping
          restores stock. Payments and refunds must be handled separately.
        </p>
      </div>
      <div className="panel">
        <div className="table-toolbar">
          <input
            aria-label="Search orders"
            type="search"
            placeholder="Search order, customer, or item…"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
          />
          <select
            aria-label="Filter order status"
            value={status}
            onChange={(e) => setStatus(e.target.value)}
          >
            <option value="">All statuses</option>
            {statuses.map((s) => (
              <option key={s}>{s}</option>
            ))}
          </select>
          <select aria-label="Sort orders" value={sort} onChange={(e) => setSort(e.target.value)}>
            <option value="newest">Newest first</option>
            <option value="oldest">Oldest first</option>
            <option value="total">Highest total</option>
          </select>
        </div>
        {loading ? (
          <Loader />
        ) : error ? (
          <ErrorState message={error} retry={reload} />
        ) : !filtered.length ? (
          <EmptyState title={orders.length ? 'No matching orders' : 'No orders yet'}>
            Orders will appear here when customers place them.
          </EmptyState>
        ) : (
          <div className="table-scroll">
            <table>
              <thead>
                <tr>
                  <th>Order / customer</th>
                  <th>Placed</th>
                  <th>Items</th>
                  <th>Total</th>
                  <th>Status</th>
                  <th>Update status</th>
                  <th>Details</th>
                </tr>
              </thead>
              <tbody>
                {filtered.map((o) => (
                  <Fragment key={o.id}>
                    <tr>
                      <td>
                        <strong className="break-id">#{o.id}</strong>
                        <small className="muted break-id">{o.userId}</small>
                      </td>
                      <td>{date(o.createdAt)}</td>
                      <td>{o.items.reduce((s, i) => s + i.quantity, 0)}</td>
                      <td>{money(o.total)}</td>
                      <td>
                        <StatusBadge status={o.status} />
                      </td>
                      <td>
                        <select
                          aria-label={`Status for order ${o.id}`}
                          value={o.status}
                          onChange={(e) => {
                            setChange({ order: o, status: e.target.value })
                            setFailure('')
                          }}
                        >
                          {[o.status, ...(orderTransitions[o.status] || [])].map((s) => (
                            <option key={s}>{s}</option>
                          ))}
                        </select>
                      </td>
                      <td>
                        <Button
                          variant="text"
                          aria-expanded={expanded === o.id}
                          onClick={() => setExpanded(expanded === o.id ? null : o.id)}
                        >
                          {expanded === o.id ? 'Close' : 'View'}
                        </Button>
                      </td>
                    </tr>
                    {expanded === o.id && (
                      <tr>
                        <td colSpan={7}>
                          <OrderDetails order={o} />
                          <Link className="btn secondary" to={`/orders/${o.id}`}>
                            Manage order
                          </Link>
                        </td>
                      </tr>
                    )}
                  </Fragment>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
      {change && (
        <Modal
          title="Update order status?"
          confirmLabel="Update status"
          onClose={() => setChange(null)}
          onConfirm={update}
          busy={busy}
        >
          <p>
            Change order #{change.order.id} from <strong>{change.order.status}</strong> to{' '}
            <strong>{change.status}</strong>?
          </p>
          <p className="muted">
            Cancellation restores stock once. Cancelling a paid order requires manual refund review.
            Mark paid only after confirming payment separately.
          </p>
          {failure && (
            <p className="form-error" role="alert">
              {failure}
            </p>
          )}
        </Modal>
      )}
    </>
  )
}
