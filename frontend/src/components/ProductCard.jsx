import { Link, useLocation, useNavigate } from 'react-router-dom'
import { useAuth } from '../context/AuthContext'
import { useCart } from '../context/CartContext'
import { Icon, ProductImage } from './common/UI'
import { money, productLink } from '../utils/format'
export function AddToCartButton({ product, quantity = 1, compact = false }) {
  const { authenticated, loading } = useAuth(),
    { addItem, busy, items } = useCart()
  const navigate = useNavigate(),
    location = useLocation()
  const remaining = product.stock - (items.find((i) => i.productId === product.id)?.quantity || 0)
  const add = () =>
    authenticated
      ? addItem(product.id, quantity)
      : navigate('/login', { state: { from: location.pathname + location.search } })
  const label =
    product.stock <= 0
      ? 'Out of stock'
      : remaining < quantity
        ? 'Stock limit reached'
        : 'Add to bag'
  return (
    <button
      className={compact ? 'quick-add' : 'btn primary'}
      onClick={add}
      disabled={loading || busy || product.stock <= 0 || remaining < quantity}
      aria-label={`${label}: ${product.name}`}
    >
      {compact ? (
        <span>+</span>
      ) : (
        <>
          {busy ? 'Updating…' : label}
          <Icon name="bag" size={18} />
        </>
      )}
    </button>
  )
}
export default function ProductCard({ product }) {
  return (
    <article className="product-card">
      <div className="product-media">
        <Link to={productLink(product.id)} aria-label={`View ${product.name}`}>
          <ProductImage src={product.imageUrl} name={product.name} />
        </Link>
        {product.stock <= 0 && !product.hasAvailableVariants && (
          <span className="stock-label">Out of stock</span>
        )}
        <>
          {product.hasVariants ? (
            <Link
              className="quick-add"
              to={productLink(product.id)}
              aria-label={`Choose options: ${product.name}`}
            >
              +
            </Link>
          ) : (
            <AddToCartButton product={product} compact />
          )}
        </>
      </div>
      <div className="product-meta">
        <span>{product.category || 'The collection'}</span>
        {product.stock > 0 && product.stock <= 3 && !product.hasVariants && (
          <span className="low-stock">Only {product.stock} left</span>
        )}
      </div>
      <div className="product-title-row">
        <h3>
          <Link to={productLink(product.id)}>{product.name}</Link>
        </h3>
        <span>{money(product.price)}</span>
      </div>
    </article>
  )
}
