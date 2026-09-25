import { useCallback, useEffect, useState } from 'react'
import { Link, useParams } from 'react-router-dom'
import { api } from '../services/api'
import { useResource } from '../hooks/useResource'
import { useCatalog } from '../context/CatalogContext'
import { useCart } from '../context/CartContext'
import ProductCard, { AddToCartButton } from '../components/ProductCard'
import { ErrorState, Loader, ProductImage, QuantityPicker, Icon } from '../components/common/UI'
import { money, categoryLink } from '../utils/format'
export default function ProductPage() {
  const { id } = useParams(),
    { products } = useCatalog(),
    { items } = useCart()
  const loader = useCallback(async () => {
    const p = await api.getProduct(id)
    return p.hasVariants ? { ...p, variants: await api.getVariants(id) } : p
  }, [id])
  const { data: baseProduct, loading, error, reload } = useResource(loader)
  const [option, setOption] = useState('')
  const product = baseProduct?.variants?.find((v) => v.id === option) || baseProduct
  const [quantity, setQuantity] = useState(1)
  useEffect(() => {
    setQuantity(1)
    setOption('')
  }, [id])
  const remaining = product
    ? Math.max(0, product.stock - (items.find((i) => i.productId === product.id)?.quantity || 0))
    : 0
  useEffect(() => setQuantity((q) => Math.max(1, Math.min(q, remaining))), [remaining])
  if (loading) return <Loader />
  if (error)
    return (
      <div className="container section">
        <Link to="/shop" className="text-link">
          ← Back to the collection
        </Link>
        <ErrorState message={error} retry={reload} />
      </div>
    )
  const related = products
    .filter((p) => p.category === product.category && p.id !== product.id)
    .slice(0, 4)
  return (
    <div className="container section">
      <nav className="breadcrumbs" aria-label="Breadcrumb">
        <Link to="/shop">The collection</Link>
        <span>/</span>
        <Link to={categoryLink(product.category)}>{product.category || 'Products'}</Link>
        <span>/</span>
        <span>{product.name}</span>
      </nav>
      <div className="product-detail">
        <div className="detail-image">
          <ProductImage src={product.imageUrl} name={product.name} />
        </div>
        <div className="detail-copy">
          <p className="eyebrow">{product.category || 'STUDIO THREAD'}</p>
          <h1>{product.name}</h1>
          <p className="detail-price">{money(product.price)}</p>
          <span className={`availability ${product.stock <= 0 ? 'unavailable' : ''}`}>
            {product.stock > 0 ? `${product.stock} in stock` : 'Out of stock'}
          </span>
          <p className="description">
            {product.description || 'Discover this piece from our current collection.'}
          </p>
          {!!baseProduct.variants?.length && (
            <label>
              Choose an option
              <select
                value={option}
                onChange={(e) => {
                  setOption(e.target.value)
                  setQuantity(1)
                }}
              >
                <option value="">Default — {money(baseProduct.price)}</option>
                {baseProduct.variants.map((v) => (
                  <option value={v.id} key={v.id}>
                    {v.variantLabel} — {money(v.price)} ({v.stock} in stock)
                  </option>
                ))}
              </select>
            </label>
          )}
          <div className="detail-actions">
            <QuantityPicker
              value={quantity}
              max={remaining}
              onChange={setQuantity}
              disabled={!remaining}
            />
            <AddToCartButton product={product} quantity={quantity} />
          </div>
          {remaining < product.stock && (
            <p className="muted">You already have {product.stock - remaining} in your bag.</p>
          )}
          <div className="detail-assurance">
            <Icon name="shield" /> Secure sign-in <span>·</span>
            <Icon name="bag" /> Simple ordering
          </div>
          <details className="product-notes">
            <summary>A note on ordering</summary>
            <p>
              Review your bag before confirming your order. Availability and prices are checked
              again when your order is placed. No online payment is collected.
            </p>
          </details>
        </div>
      </div>
      {related.length > 0 && (
        <section className="section">
          <div className="section-heading">
            <h2>A little more to discover</h2>
            <Link className="text-link" to={categoryLink(product.category)}>
              Explore {product.category} ↗
            </Link>
          </div>
          <div className="product-grid">
            {related.map((p) => (
              <ProductCard key={p.id} product={p} />
            ))}
          </div>
        </section>
      )}
    </div>
  )
}
