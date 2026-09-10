import { Link } from 'react-router-dom'

export default function ProductCard({ product }) {
  const outOfStock = product.stock <= 0

  return (
    <Link to={`/product/${product.id}`} className="product-card">
      <div className="product-card__image-wrap">
        {product.imageUrl ? (
          <img className="product-card__image" src={product.imageUrl} alt={product.name} />
        ) : (
          <div className="product-card__image" style={{ background: 'var(--surface)' }} />
        )}
      </div>
      <div className="product-card__info">
        <div className="product-card__name">{product.name}</div>
        <div className="product-card__price">₹{product.price.toFixed(2)}</div>
        {outOfStock && <div className="product-card__stock-warning">Out of stock</div>}
      </div>
    </Link>
  )
}
