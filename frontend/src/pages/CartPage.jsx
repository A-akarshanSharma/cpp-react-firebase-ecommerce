import { useEffect } from 'react'
import { Link } from 'react-router-dom'
import { useCart } from '../context/CartContext'
import {
  Button,
  EmptyState,
  ErrorState,
  Icon,
  Loader,
  PageHeading,
  ProductImage,
  QuantityPicker,
} from '../components/common/UI'
import { money, productLink } from '../utils/format'
export function CartSummary({ total, children, shippingFee = 0, showShipping = false }) {
  return (
    <aside className="order-summary">
      <p className="eyebrow">THE GOOD THINGS ADD UP</p>
      <h2>Order summary</h2>
      <div className="summary-line">
        <span>Items subtotal</span>
        <span>{money(total - shippingFee)}</span>
      </div>
      {(showShipping || shippingFee > 0) && (
        <div className="summary-line">
          <span>Shipping</span>
          <span>{money(shippingFee)}</span>
        </div>
      )}
      <div className="summary-line summary-total">
        <strong>Total</strong>
        <strong>{money(total)}</strong>
      </div>
      <p className="field-help">Order total only. No online payment is collected.</p>
      {children}
      <div className="summary-note">
        <Icon name="shield" size={17} /> Review first. Confirm when you’re ready.
      </div>
    </aside>
  )
}
export default function CartPage() {
  const { items, total, loading, busy, error, refresh, updateItem, removeItem, itemCount } =
    useCart()
  useEffect(() => {
    refresh()
  }, [refresh])
  return (
    <div className="container section">
      <PageHeading
        eyebrow="YOUR EVERYDAY FINDS"
        title="Your shopping bag"
        action={
          <Link className="text-link" to="/shop">
            Continue shopping <Icon name="arrow" />
          </Link>
        }
      >
        {itemCount} {itemCount === 1 ? 'item' : 'items'}, chosen by you.
      </PageHeading>
      {error ? (
        <ErrorState message={error} retry={refresh} />
      ) : loading && !items.length ? (
        <Loader />
      ) : !items.length ? (
        <EmptyState title="A little room for something good" to="/shop">
          Your bag is empty. Let’s find your next favorite.
        </EmptyState>
      ) : (
        <div className="cart-layout">
          <div className="cart-items">
            {items.map((item) => (
              <article className="cart-item" key={item.productId}>
                <Link to={productLink(item.productId)}>
                  <ProductImage src={item.imageUrl} name={item.name} />
                </Link>
                <div className="cart-item-info">
                  <Link to={productLink(item.productId)}>
                    <h3>{item.name}</h3>
                  </Link>
                  <p className="muted">{money(item.price)} each</p>
                  {(!item.available || item.quantity > item.stock || item.quantity <= 0) && (
                    <p className="form-error">
                      {item.stock <= 0
                        ? 'Unavailable. Please remove this item.'
                        : `Only ${item.stock} available. Reduce your quantity.`}
                    </p>
                  )}
                  <div className="cart-item-controls">
                    <QuantityPicker
                      value={item.quantity}
                      max={item.stock}
                      onChange={(q) => updateItem(item.productId, q)}
                      disabled={busy || loading}
                    />
                    <Button
                      variant="text"
                      disabled={busy || loading}
                      onClick={() => removeItem(item.productId)}
                    >
                      Remove
                    </Button>
                  </div>
                </div>
                <strong>{money(item.price * item.quantity)}</strong>
              </article>
            ))}
          </div>
          <CartSummary total={total}>
            {items.some((i) => !i.available || i.quantity <= 0 || i.quantity > i.stock) ? (
              <p className="form-error">Update unavailable items before continuing.</p>
            ) : (
              <Link
                className={`btn primary ${busy || loading ? 'disabled-link' : ''}`}
                aria-disabled={busy || loading}
                to="/checkout"
                onClick={(e) => {
                  if (busy || loading) e.preventDefault()
                }}
              >
                Proceed to checkout
                <Icon name="arrow" />
              </Link>
            )}
          </CartSummary>
        </div>
      )}
    </div>
  )
}
