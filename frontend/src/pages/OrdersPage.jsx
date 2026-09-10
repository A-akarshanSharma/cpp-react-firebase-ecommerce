import { useEffect, useState } from 'react'
import { useLocation, Link } from 'react-router-dom'
import { api } from '../services/api'
import { useAuth } from '../context/AuthContext'

export default function OrdersPage() {
  const { user } = useAuth()
  const location = useLocation()
  const justPlacedOrderId = location.state?.justPlacedOrderId

  const [orders, setOrders] = useState([])
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    if (!user) return
    api.getMyOrders()
      .then((data) => setOrders(data.sort((a, b) => b.createdAt - a.createdAt)))
      .finally(() => setLoading(false))
  }, [user])

  if (!user) {
    return (
      <div className="page-container">
        <div className="empty-state">
          <h3>Sign in to view your orders</h3>
          <Link to="/login" className="btn btn--primary" style={{ marginTop: 16 }}>Sign in</Link>
        </div>
      </div>
    )
  }

  return (
    <div className="page-container cart-page">
      <h1>Your orders</h1>

      {justPlacedOrderId && (
        <p className="meta" style={{ color: 'var(--sage)', marginBottom: 24 }}>
          Order placed successfully.
        </p>
      )}

      {loading && <p className="meta">Loading…</p>}

      {!loading && orders.length === 0 && (
        <div className="empty-state">
          <h3>No orders yet</h3>
          <Link to="/" className="btn btn--primary" style={{ marginTop: 16 }}>Browse shop</Link>
        </div>
      )}

      {orders.map((order) => (
        <div key={order.id} className="order-line">
          <div>
            <div className="cart-line__name">Order #{order.id.slice(0, 8)}</div>
            <div className="meta">{new Date(order.createdAt * 1000).toLocaleString()}</div>
            <div className="meta" style={{ marginTop: 6 }}>
              {order.items.map((item) => `${item.name} × ${item.quantity}`).join(', ')}
            </div>
          </div>
          <div className="meta" style={{ textTransform: 'capitalize' }}>{order.status}</div>
          <div className="cart-line__price">₹{order.total.toFixed(2)}</div>
        </div>
      ))}
    </div>
  )
}
