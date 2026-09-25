import { useSearchParams } from 'react-router-dom'
import { useCatalog } from '../context/CatalogContext'
import { EmptyState, ErrorState, Loader, PageHeading, Button } from '../components/common/UI'
import ProductCard from '../components/ProductCard'
import { filterProducts } from '../utils/format'
export default function ShopPage() {
  const { products, categories, loading, error, reload } = useCatalog()
  const [params, setParams] = useSearchParams()
  const set = (key, value) =>
    setParams(
      (p) => {
        const next = new URLSearchParams(p)
        value ? next.set(key, value) : next.delete(key)
        return next
      },
      { replace: true },
    )
  const results = filterProducts(products, params)
  return (
    <div className="container section">
      <PageHeading eyebrow="THE STUDIO COLLECTION" title="Good finds start here.">
        Explore the collection. Find your everyday favorites.
      </PageHeading>
      <div className="shop-layout">
        <aside className="filters">
          <div className="filter-title">
            <h2>Refine your search</h2>
            <Button variant="text" onClick={() => setParams({})}>
              Clear all
            </Button>
          </div>
          <label>
            Search
            <input
              type="search"
              placeholder="Name, category, or description"
              value={params.get('q') || ''}
              onChange={(e) => set('q', e.target.value)}
            />
          </label>
          <label>
            Category
            <select
              aria-label="Category"
              value={params.get('category') || ''}
              onChange={(e) => set('category', e.target.value)}
            >
              <option value="">All categories</option>
              {categories.map((c) => (
                <option key={c}>{c}</option>
              ))}
            </select>
          </label>
          <fieldset>
            <legend>Price range (₹)</legend>
            <div className="price-inputs">
              <label>
                <span className="sr-only">Minimum price</span>
                <input
                  aria-label="Minimum price"
                  type="number"
                  min="0"
                  placeholder="Min"
                  value={params.get('min') || ''}
                  onChange={(e) => set('min', e.target.value)}
                />
              </label>
              <span>—</span>
              <label>
                <span className="sr-only">Maximum price</span>
                <input
                  aria-label="Maximum price"
                  type="number"
                  min="0"
                  placeholder="Max"
                  value={params.get('max') || ''}
                  onChange={(e) => set('max', e.target.value)}
                />
              </label>
            </div>
          </fieldset>
          <label className="checkbox">
            <input
              type="checkbox"
              checked={params.get('stock') === '1'}
              onChange={(e) => set('stock', e.target.checked ? '1' : '')}
            />{' '}
            In stock only
          </label>
          <div className="filter-note">
            A considered collection.
            <br />
            Something for your everyday.
          </div>
        </aside>
        <div>
          <div className="catalog-toolbar">
            <span className="muted" aria-live="polite">
              {loading
                ? 'Finding good things…'
                : `${results.length} ${results.length === 1 ? 'product' : 'products'}`}
            </span>
            <label className="sort-control">
              Sort by
              <select
                aria-label="Sort by"
                value={params.get('sort') || 'name-asc'}
                onChange={(e) => set('sort', e.target.value)}
              >
                <option value="name-asc">Name A → Z</option>
                <option value="name-desc">Name Z → A</option>
                <option value="price-asc">Price low → high</option>
                <option value="price-desc">Price high → low</option>
              </select>
            </label>
          </div>
          {loading ? (
            <Loader cards />
          ) : error ? (
            <ErrorState message={error} retry={reload} />
          ) : !products.length ? (
            <EmptyState title="The collection is coming together">
              There are no products available yet.
            </EmptyState>
          ) : !results.length ? (
            <EmptyState title="No finds this time">
              <span>Try a different search or </span>
              <button className="inline-link" onClick={() => setParams({})}>
                clear your filters
              </button>
              .
            </EmptyState>
          ) : (
            <div className="product-grid shop-grid">
              {results.map((p) => (
                <ProductCard key={p.id} product={p} />
              ))}
            </div>
          )}
        </div>
      </div>
    </div>
  )
}
