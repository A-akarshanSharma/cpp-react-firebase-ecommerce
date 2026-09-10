import { useEffect, useState } from 'react'
import ProductCard from '../components/ProductCard'
import { api } from '../services/api'

export default function HomePage() {
  const [products, setProducts] = useState([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(null)

  useEffect(() => {
    api.listProducts()
      .then(setProducts)
      .catch((err) => setError(err.message))
      .finally(() => setLoading(false))
  }, [])

  return (
    <div className="page-container">
      <section className="hero">
        <div>
          <div className="hero__eyebrow">New season</div>
          <h1 className="hero__title">Considered pieces, made to be worn often.</h1>
          <p className="hero__copy">
            Small-batch clothing, cut for everyday use rather than the season's headline trend.
          </p>
        </div>
        <img
          className="hero__image"
          src={products[0]?.imageUrl || 'https://images.unsplash.com/photo-1523381210434-271e8be1f52b?w=800'}
          alt=""
        />
      </section>

      <div className="section-heading">
        <h2>Shop the collection</h2>
      </div>

      {loading && <p className="meta">Loading products…</p>}
      {error && <p className="error-text">Couldn't load products: {error}</p>}

      {!loading && !error && products.length === 0 && (
        <div className="empty-state">
          <h3>No products yet</h3>
          <p>Once products are added from the admin panel, they'll show up here.</p>
        </div>
      )}

      {products.length > 0 && (
        <div className="product-grid">
          {products.map((p) => (
            <ProductCard key={p.id} product={p} />
          ))}
        </div>
      )}
    </div>
  )
}
