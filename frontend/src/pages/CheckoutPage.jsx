import { useEffect, useRef, useState } from 'react'
import { Link } from 'react-router-dom'
import { useCart } from '../context/CartContext'
import { useCatalog } from '../context/CatalogContext'
import {
  Button,
  EmptyState,
  ErrorState,
  Icon,
  Loader,
  PageHeading,
  ProductImage,
} from '../components/common/UI'
import { CartSummary } from './CartPage'
import AddressFields, { emptyAddress } from '../components/orders/AddressFields'
import { savedCheckoutAttempt } from '../services/checkoutAttempt'
import { useAuth } from '../context/AuthContext'
import { api } from '../services/api'
import { useResource } from '../hooks/useResource'
import { money } from '../utils/format'
export default function CheckoutPage() {
  const { items, total, loading, busy, error, refresh, placeOrder } = useCart(),
    { reload } = useCatalog()
  const [order, setOrder] = useState(null),
    [failure, setFailure] = useState(''),
    [confirmed, setConfirmed] = useState(false),
    [uncertain, setUncertain] = useState(false)
  const { profile } = useAuth()
  const [address, setAddress] = useState(
    () => savedCheckoutAttempt(profile?.uid)?.shippingAddress || { ...emptyAddress },
  )
  const shipping = useResource(api.listShipping)
  const [shippingId, setShippingId] = useState(
    () => savedCheckoutAttempt(profile?.uid)?.shippingMethodId || '',
  )
  const methods = (shipping.data || []).filter(
    (m) => m.kind === 'pickup' || !m.countries.length || m.countries.includes(address.country),
  )
  const selectedShipping = methods.find((m) => m.id === shippingId)
  const pickup = selectedShipping?.kind === 'pickup'
  const [contact, setContact] = useState(
    () => savedCheckoutAttempt(profile?.uid)?.pickupContact || { name: '', phone: '' },
  )
  const reviewVersion = JSON.stringify([
    items.map((i) => [i.productId, i.quantity, i.price, i.quoteVersion]),
    selectedShipping?.id,
    selectedShipping?.feeMinor,
  ])
  useEffect(() => {
    setConfirmed(false)
  }, [reviewVersion])
  const submitted = useRef(false)
  useEffect(() => {
    refresh()
  }, [refresh])
  const place = async (event) => {
    event.preventDefault()
    if (submitted.current || !confirmed) return
    submitted.current = true
    setFailure('')
    try {
      const result = await placeOrder(
        pickup ? undefined : address,
        selectedShipping,
        pickup ? contact : undefined,
      )
      if (result) {
        setOrder(result)
        reload()
      }
    } catch (e) {
      setConfirmed(false)
      setFailure(e.message)
      if (!e.status || e.status >= 500 || e.status === 200) setUncertain(true)
      refresh()
      shipping.reload()
    } finally {
      submitted.current = false
    }
  }
  if (order)
    return (
      <div className="container section">
        <div className="confirmation">
          <span className="state-icon">
            <Icon name="check" size={36} />
          </span>
          <p className="eyebrow">A LITTLE SOMETHING TO LOOK FORWARD TO</p>
          <h1>Your order is in.</h1>
          <p>Thank you for shopping with Studio Thread.</p>
          <p className="muted">Order #{order.id}</p>
          <p className="detail-price">{money(order.total)}</p>
          <p>No payment has been collected. Your order is pending.</p>
          {order.expiresAt && (
            <p>
              Confirm payment with the store before{' '}
              {new Date(order.expiresAt * 1000).toLocaleString()} to keep your reservation.
            </p>
          )}
          <div className="actions">
            <Link to="/orders" className="btn primary">
              View your orders <Icon name="arrow" />
            </Link>
            <Link to="/shop" className="btn secondary">
              Keep exploring
            </Link>
          </div>
        </div>
      </div>
    )
  const invalid =
    !items.length || items.some((i) => !i.available || i.quantity <= 0 || i.quantity > i.stock)
  return (
    <div className="container section">
      <PageHeading eyebrow="ONE LAST LOOK" title="Checkout">
        Enter your delivery address, choose shipping and review your total before confirming.
      </PageHeading>
      {failure && (
        <div className="error-state" role="alert">
          <p>{failure}</p>
          {uncertain && (
            <p>
              Please{' '}
              <Link className="inline-link" to="/orders">
                check your order history
              </Link>{' '}
              before placing another order.
            </p>
          )}
        </div>
      )}
      {error ? (
        <ErrorState message={error} retry={refresh} />
      ) : loading ? (
        <Loader />
      ) : !items.length ? (
        <EmptyState title="Your bag is empty" to="/shop">
          Add something you love before placing an order.
        </EmptyState>
      ) : (
        <div className="cart-layout">
          <section>
            <div className="review-heading">
              <h2>Your items</h2>
              <Link className="text-link" to="/cart">
                Edit bag
              </Link>
            </div>
            {items.map((i) => (
              <div className="review-item" key={i.productId}>
                <ProductImage src={i.imageUrl} name={i.name} />
                <div>
                  <h3>{i.name}</h3>
                  <p className="muted">
                    {i.quantity} × {money(i.price)}
                  </p>
                  {!i.available && <p className="form-error">This quantity is unavailable.</p>}
                </div>
                <strong>{money(i.price * i.quantity)}</strong>
              </div>
            ))}
            <form id="delivery-checkout" onSubmit={place}>
              {!pickup && (
                <AddressFields
                  value={address}
                  onChange={(value) => {
                    setAddress(value)
                    setConfirmed(false)
                  }}
                  disabled={busy || uncertain}
                />
              )}
              {pickup && (
                <fieldset disabled={busy || uncertain} className="address-fields">
                  <legend>Pickup contact</legend>
                  {['name', 'phone'].map((field) => (
                    <label key={field}>
                      {field === 'name' ? 'Pickup name' : 'Pickup phone'}
                      <input
                        required
                        maxLength={field === 'name' ? 160 : 40}
                        type={field === 'phone' ? 'tel' : 'text'}
                        value={contact[field]}
                        onChange={(e) => {
                          setContact({ ...contact, [field]: e.target.value })
                          setConfirmed(false)
                        }}
                      />
                    </label>
                  ))}
                  <p>{selectedShipping.pickupInstructions}</p>
                </fieldset>
              )}
              {shipping.error ? (
                <ErrorState message={shipping.error} retry={shipping.reload} />
              ) : shipping.loading ? (
                <p>Loading shipping options…</p>
              ) : shipping.data?.length > 0 ? (
                <label>
                  Shipping method
                  <select
                    aria-label="Shipping method"
                    required
                    value={shippingId}
                    disabled={busy || uncertain}
                    onChange={(e) => {
                      setShippingId(e.target.value)
                      setConfirmed(false)
                    }}
                  >
                    <option value="">Select shipping</option>
                    {methods.map((m) => (
                      <option key={m.id} value={m.id}>
                        {m.name} — {money(m.fee)} {m.estimatedDays}
                      </option>
                    ))}
                  </select>
                  {!methods.length && (
                    <span className="form-error">No shipping methods serve this country.</span>
                  )}
                </label>
              ) : (
                <p className="muted">
                  Checkout is temporarily unavailable. Please contact the store.
                </p>
              )}
            </form>
            <div className="notice">
              <Icon name="bag" />
              <p>
                This step creates your order. Your delivery or pickup details are saved with the
                order. Pending orders hold stock for 4 hours. Payment is arranged separately; no
                payment is collected here.
              </p>
            </div>
          </section>
          <CartSummary
            total={total + (selectedShipping?.feeMinor || 0) / 100}
            shippingFee={(selectedShipping?.feeMinor || 0) / 100}
            showShipping
          >
            {!pickup && address.name && address.line1 && (
              <div className="checkout-address">
                <h3>Deliver to</h3>
                <p>
                  {address.name}
                  <br />
                  {address.line1}
                  {address.line2 && (
                    <>
                      <br />
                      {address.line2}
                    </>
                  )}
                  <br />
                  {[address.city, address.region, address.postalCode].filter(Boolean).join(', ')}
                  <br />
                  {address.country}
                  <br />
                  {address.phone}
                </p>
                {selectedShipping && (
                  <p className="muted">
                    {selectedShipping.name} {selectedShipping.estimatedDays}
                  </p>
                )}
              </div>
            )}
            {pickup && (
              <div className="checkout-address">
                <h3>Store pickup</h3>
                <p>
                  {selectedShipping.name}
                  <br />
                  {selectedShipping.pickupInstructions}
                  <br />
                  {contact.name}
                  <br />
                  {contact.phone}
                </p>
              </div>
            )}
            <label className="checkbox">
              <input
                type="checkbox"
                checked={confirmed}
                onChange={(e) => setConfirmed(e.target.checked)}
                disabled={busy}
              />{' '}
              I have reviewed my items and total.
            </label>
            {invalid && <p className="form-error">Please update unavailable items in your bag.</p>}
            <Button
              type="submit"
              form="delivery-checkout"
              disabled={
                !confirmed ||
                invalid ||
                busy ||
                uncertain ||
                shipping.loading ||
                !!shipping.error ||
                !selectedShipping
              }
            >
              {busy ? 'Placing your order…' : 'Confirm order'}
              <Icon name="arrow" />
            </Button>
          </CartSummary>
        </div>
      )}
    </div>
  )
}
