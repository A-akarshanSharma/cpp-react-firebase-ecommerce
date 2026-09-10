import { useState } from 'react'
import { Link, useNavigate } from 'react-router-dom'
import { useCart } from '../context/CartContext'
import { useAuth } from '../context/AuthContext'
import { api } from '../services/api'

export default function CartPage() {
  const { user } = useAuth()
  const { items, total, updateItem, removeItem, refresh } = useCart()
  const navigate = useNavigate()
  const [placing, setPlacing] = useState(false)
  const [orderError, setOrderError] = useState(null)

  const handlePlaceOrder = async () => {
    setPlacing(true)
    setOrderError(null)
    try {
      const order = await api.createOrder()
      await refresh()
      navigate('/orders', { state: { justPlacedOrderId: order.id } })
    } catch (err) {
      setOrderError(err.data?.error || err.message)
    } finally {
      setPlacing(false)
    }
  }

  if (!user) {
    return (
      <div className="page-container">
        <div className="empty-state">
          <h3>Sign in to view your cart</h3>
          <Link to="/login" className="btn btn--primary" style={{ marginTop: 16 }}>Sign in</Link>
        </div>
      </div>
    )
  }

  if (items.length === 0) {
    return (
      <div className="page-container">
        <div className="empty-state">
          <h3>Your cart is empty</h3>
          <p>Find something you like and it'll show up here.</p>
          <Link to="/" className="btn btn--primary" style={{ marginTop: 16 }}>Browse shop</Link>
        </div>
      </div>
    )
  }

  const hasUnavailable = items.some((item) => !item.available)

  return (
    <div className="page-container cart-page">
      <h1>Your cart</h1>

      {items.map((item) => (
        <div className="cart-line" key={item.productId}>
          {item.imageUrl ? (
            <img className="cart-line__image" src={item.imageUrl} alt={item.name} />
          ) : (
            <div className="cart-line__image" />
          )}
          <div>
            <div className="cart-line__name">{item.name}</div>
            {!item.available && (
              <div className="cart-line__unavailable">
                {item.stock === 0 ? 'No longer available' : `Only ${item.stock} left — reduce quantity`}
              </div>
            )}
            <div className="cart-line__price">₹{item.price.toFixed(2)} each</div>
          </div>
          <div className="cart-line__actions">
            <div className="quantity-picker">
              <button onClick={() => updateItem(item.productId, Math.max(1, item.quantity - 1))}>−</button>
              <span>{item.quantity}</span>
              <button onClick={() => updateItem(item.productId, item.quantity + 1)}>+</button>
            </div>
            <button className="cart-line__remove" onClick={() => removeItem(item.productId)}>Remove</button>
          </div>
        </div>
      ))}

      <div className="cart-summary">
        <div className="cart-summary__row">
          <span>Subtotal</span>
          <span>₹{total.toFixed(2)}</span>
        </div>
        <div className="cart-summary__row cart-summary__total">
          <span>Total</span>
          <span>₹{total.toFixed(2)}</span>
        </div>

        {orderError && <p className="error-text" style={{ marginTop: 12 }}>{orderError}</p>}
        {hasUnavailable && (
          <p className="error-text" style={{ marginTop: 12 }}>
            Resolve the unavailable items above before placing your order.
          </p>
        )}

        <button
          className="btn btn--primary btn--full"
          style={{ marginTop: 20 }}
          onClick={handlePlaceOrder}
          disabled={placing || hasUnavailable}
        >
          {placing ? 'Placing order…' : 'Place order'}
        </button>
        <p className="meta" style={{ marginTop: 12 }}>
          Payment isn't wired in yet — this creates the order directly. Real checkout/payment comes next.
        </p>
      </div>
    </div>
  )
}
