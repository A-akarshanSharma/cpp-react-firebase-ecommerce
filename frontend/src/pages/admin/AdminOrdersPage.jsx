import { useEffect, useState } from 'react'
import { api } from '../../services/api'

const STATUSES = ['pending', 'paid', 'shipped', 'cancelled']

export default function AdminOrdersPage() {
  const [orders, setOrders] = useState([])
  const [loading, setLoading] = useState(true)
  const [updatingId, setUpdatingId] = useState(null)

  const load = () => {
    setLoading(true)
    api.getAllOrders()
      .then((data) => setOrders(data.sort((a, b) => b.createdAt - a.createdAt)))
      .finally(() => setLoading(false))
  }

  useEffect(load, [])

  const handleStatusChange = async (orderId, status) => {
    setUpdatingId(orderId)
    try {
      await api.updateOrderStatus(orderId, status)
      setOrders((prev) => prev.map((o) => (o.id === orderId ? { ...o, status } : o)))
    } finally {
      setUpdatingId(null)
    }
  }

  return (
    <div>
      <div className="admin-header">
        <h2>Orders</h2>
      </div>

      {loading ? (
        <p className="meta">Loading…</p>
      ) : orders.length === 0 ? (
        <div className="empty-state">
          <h3>No orders yet</h3>
        </div>
      ) : (
        <table className="admin-table">
          <thead>
            <tr>
              <th>Order</th>
              <th>Customer</th>
              <th>Items</th>
              <th>Total</th>
              <th>Placed</th>
              <th>Status</th>
            </tr>
          </thead>
          <tbody>
            {orders.map((o) => (
              <tr key={o.id}>
                <td className="meta">#{o.id.slice(0, 8)}</td>
                <td className="meta">{o.userId.slice(0, 10)}…</td>
                <td>{o.items.map((i) => `${i.name} × ${i.quantity}`).join(', ')}</td>
                <td className="admin-table__num">₹{o.total.toFixed(2)}</td>
                <td className="meta">{new Date(o.createdAt * 1000).toLocaleDateString()}</td>
                <td>
                  <select
                    className="admin-select"
                    value={o.status}
                    disabled={updatingId === o.id}
                    onChange={(e) => handleStatusChange(o.id, e.target.value)}
                  >
                    {STATUSES.map((s) => (
                      <option key={s} value={s}>{s}</option>
                    ))}
                  </select>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </div>
  )
}
