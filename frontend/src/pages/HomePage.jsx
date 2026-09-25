import { Link } from 'react-router-dom'
import { useCatalog } from '../context/CatalogContext'
import ProductCard from '../components/ProductCard'
import { EmptyState, ErrorState, Icon, Loader } from '../components/common/UI'
import { categoryLink } from '../utils/format'
export default function HomePage() {
  const { products, categories, loading, error, reload } = useCatalog()
  return (
    <>
      <section className="hero container">
        <div className="hero-copy">
          <p className="eyebrow">
            <span className="tiny-line" /> THE EVERYDAY, CONSIDERED
          </p>
          <h1>
            Less ordinary.
            <br />
            More <em>you.</em>
          </h1>
          <p>
            Thoughtful finds for the way you live.
            <br />
            Discover pieces that make the everyday feel a little more special.
          </p>
          <div className="hero-actions">
            <Link to="/shop" className="btn primary">
              Explore the collection <Icon name="arrow" />
            </Link>
            <Link to="/about" className="text-link">
              Meet the studio ↗
            </Link>
          </div>
          <div className="hero-note">
            <span className="mini-sun">✳</span>
            <span>
              A little intention.
              <br />
              <strong>A world of difference.</strong>
            </span>
          </div>
        </div>
        <div
          className="hero-art"
          aria-label="Abstract studio composition in terracotta, cream and sage"
          role="img"
        >
          <div className="art-grid" />
          <div className="art-orbit" />
          <div className="art-arch" />
          <div className="art-circle" />
          <div className="art-plinth" />
          <div className="art-vase">
            <div />
          </div>
          <div className="art-ball" />
          <span className="art-word">
            everyday
            <br />
            <em>well made.</em>
          </span>
          <span className="art-caption">THE STUDIO EDIT — A FRESH PERSPECTIVE</span>
          <span className="art-stamp">
            ST
            <br />↗
          </span>
        </div>
      </section>
      <div className="values-strip">
        <div className="container">
          <span>
            <Icon name="sun" /> Thoughtfully selected
          </span>
          <span>
            <Icon name="shield" /> Secure sign-in
          </span>
          <span>
            <Icon name="bag" /> Simple ordering
          </span>
          <span>
            <Icon name="heart" /> Everyday inspiration
          </span>
        </div>
      </div>
      <section className="container section">
        <div className="section-heading">
          <div>
            <p className="eyebrow">FIND YOUR KIND OF GOOD</p>
            <h2>Explore the collection</h2>
          </div>
          <Link to="/shop" className="text-link">
            Shop all <Icon name="arrow" size={18} />
          </Link>
        </div>
        {loading ? (
          <Loader cards />
        ) : error ? (
          <ErrorState message={error} retry={reload} />
        ) : !products.length ? (
          <EmptyState title="Something good is on its way">
            Our collection is being prepared. Check back soon.
          </EmptyState>
        ) : (
          <>
            <div className="category-grid">
              {categories.map((c, i) => (
                <Link key={c} to={categoryLink(c)} className={`category-card tone-${i % 4}`}>
                  <span className="category-number">0{i + 1}</span>
                  <Icon name={['sun', 'box', 'heart', 'grid'][i % 4]} size={42} />
                  <div>
                    <h3>{c}</h3>
                    <Icon name="arrow" />
                  </div>
                  <span>{products.filter((p) => p.category === c).length} pieces to discover</span>
                </Link>
              ))}
            </div>
            <div className="section-heading featured-heading">
              <div>
                <p className="eyebrow">A FEW GOOD FINDS</p>
                <h2>The studio selection</h2>
                <p className="muted">A small selection from our current collection.</p>
              </div>
              <Link to="/shop" className="text-link">
                View the collection <Icon name="arrow" size={18} />
              </Link>
            </div>
            <div className="product-grid">
              {products.slice(0, 8).map((p) => (
                <ProductCard key={p.id} product={p} />
              ))}
            </div>
          </>
        )}
      </section>
      <section className="container story-banner">
        <div className="story-emblem">
          st<span>✳</span>
        </div>
        <div>
          <p className="eyebrow">MORE THAN A FULL SHOPPING BAG</p>
          <h2>
            Make room for things
            <br />
            that feel like you.
          </h2>
          <p>
            We believe a good find is one that belongs in your everyday.
            <br />
            Explore at your own pace. Find what speaks to you.
          </p>
          <Link className="text-link" to="/about">
            A little about us <Icon name="arrow" />
          </Link>
        </div>
      </section>
      <section className="container benefits section">
        {[
          ['shield', 'Secure shopping', 'Your account, protected with secure sign-in.'],
          ['sun', 'Considered products', 'Find the details you need to choose with confidence.'],
          ['bag', 'Easy ordering', 'From your bag to your order in a few simple steps.'],
          ['chat', 'Here to help', 'Visit our customer service guide for the next step.'],
        ].map(([icon, title, text]) => (
          <div key={title}>
            <Icon name={icon} size={28} />
            <h3>{title}</h3>
            <p>{text}</p>
          </div>
        ))}
      </section>
    </>
  )
}
