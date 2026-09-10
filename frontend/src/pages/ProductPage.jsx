import { useEffect, useState } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { api } from '../services/api'
import { useAuth } from '../context/AuthContext'
import { useCart } from '../context/CartContext'

export default function ProductPage() {
  const { id } = useParams()
  const navigate = useNavigate()
  const { user } = useAuth()
  const { addItem } = useCart()

  const [product, setProduct] = useState(null)
  const [quantity, setQuantity] = useState(1)
  const [loading, setLoading] = useState(true)
  const [message, setMessage] = useState(null)
  const [adding, setAdding] = useState(false)

  useEffect(() => {
    setLoading(true)
    api.getProduct(id).then(setProduct).finally(() => setLoading(false))
  }, [id])

  const handleAddToCart = async () => {
    if (!user) {
      navigate('/login')
      return
    }
    setAdding(true)
    setMessage(null)
    const result = await addItem(product.id, quantity)
    setAdding(false)
    if (result.ok) {
      setMessage({ type: 'success', text: 'Added to cart.' })
    } else {
      setMessage({
        type: 'error',
        text: result.available !== undefined
          ? `Only ${result.available} left in stock.`
          : result.error,
      })
    }
  }

  if (loading) return <div className="page-container"><p className="meta">Loading…</p></div>
  if (!product) return <div className="page-container"><p className="error-text">Product not found.</p></div>

  const outOfStock = product.stock <= 0

  return (
    <div className="page-container">
      <div className="product-detail">
        {product.imageUrl ? (
          <img className="product-detail__image" src={product.imageUrl} alt={product.name} />
        ) : (
          <div className="product-detail__image" />
        )}

        <div>
          <div className="product-detail__category">{product.category}</div>
          <h1>{product.name}</h1>
          <div className="product-detail__price">₹{product.price.toFixed(2)}</div>
          <p className="product-detail__description">{product.description}</p>

          <div className={`product-detail__stock ${product.stock > 3 ? 'product-detail__stock--ok' : 'product-detail__stock--low'}`}>
            {outOfStock ? 'Out of stock' : product.stock <= 3 ? `Only ${product.stock} left` : 'In stock'}
          </div>

          {!outOfStock && (
            <div className="product-detail__actions">
              <div className="quantity-picker">
                <button onClick={() => setQuantity((q) => Math.max(1, q - 1))} aria-label="Decrease quantity">−</button>
                <span>{quantity}</span>
                <button onClick={() => setQuantity((q) => Math.min(product.stock, q + 1))} aria-label="Increase quantity">+</button>
              </div>
              <button className="btn btn--primary" onClick={handleAddToCart} disabled={adding}>
                {adding ? 'Adding…' : 'Add to cart'}
              </button>
            </div>
          )}

          {message && (
            <p className={message.type === 'error' ? 'error-text' : 'meta'} style={{ marginTop: 16 }}>
              {message.text}
            </p>
          )}
        </div>
      </div>
    </div>
  )
}
