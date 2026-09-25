import { money, date } from '../../utils/format'
export default function OrderDetails({ order }) {
  return (
    <div className="order-details">
      {order.status === 'pending' && order.expiresAt && (
        <p className="notice">
          Reservation expires: {new Date(order.expiresAt * 1000).toLocaleString()}. Contact the
          store to confirm payment before then.
        </p>
      )}
      {order.status === 'expired' && (
        <p className="notice">This pending order expired. Its reserved stock has been released.</p>
      )}
      {order.shippingMethod && (
        <p>
          Shipping: {order.shippingMethod.name} · {money((order.shippingFeeMinor || 0) / 100)}
        </p>
      )}
      {order.shipment && (
        <section>
          <h3>Tracking</h3>
          <p>
            {order.shipment.carrier} · {order.shipment.trackingNumber}
          </p>
          {order.shipment.trackingUrl?.startsWith('https://') && (
            <a
              className="text-link"
              href={order.shipment.trackingUrl}
              target="_blank"
              rel="noopener noreferrer"
            >
              Track shipment
            </a>
          )}
        </section>
      )}
      {order.shippingMethod?.kind === 'pickup' ? (
        <section>
          <h3>Store pickup</h3>
          <p>{order.shippingMethod.pickupInstructions}</p>
          <p>
            {order.pickupContact?.name} · {order.pickupContact?.phone}
          </p>
          <p>
            {order.status === 'shipped'
              ? 'Ready for pickup'
              : order.status === 'delivered'
                ? 'Collected'
                : ''}
          </p>
        </section>
      ) : order.shippingAddress ? (
        <section>
          <h3>Delivery address</h3>
          <address>
            {order.shippingAddress.name}
            <br />
            {order.shippingAddress.line1}
            <br />
            {order.shippingAddress.line2 && (
              <>
                {order.shippingAddress.line2}
                <br />
              </>
            )}
            {order.shippingAddress.city}, {order.shippingAddress.region}{' '}
            {order.shippingAddress.postalCode}
            <br />
            {order.shippingAddress.country}
            <br />
            {order.shippingAddress.phone}
          </address>
        </section>
      ) : (
        <p className="muted">
          No delivery address was recorded. Contact the store before shipping.
        </p>
      )}
      {order.refundStatus === 'manual_review_required' && (
        <p className="notice">
          A manual refund review is required. No automatic refund has been issued.
        </p>
      )}
      {order.statusHistory?.length > 0 && (
        <section>
          <h3>Order history</h3>
          <ol>
            {order.statusHistory.map((entry, index) => (
              <li key={index}>
                {entry.to} · {date(entry.createdAt)}
                {entry.reason && ` — ${entry.reason}`}
              </li>
            ))}
          </ol>
        </section>
      )}
      <div className="table-scroll">
        <table>
          <thead>
            <tr>
              <th>Item</th>
              <th>Quantity</th>
              <th>Price at order</th>
              <th>Subtotal</th>
            </tr>
          </thead>
          <tbody>
            {order.items.map((i, index) => (
              <tr key={`${i.productId}-${index}`}>
                <td>
                  {i.name}
                  {i.sku && <small className="muted">SKU: {i.sku}</small>}
                </td>
                <td>{i.quantity}</td>
                <td>{money(i.price)}</td>
                <td>{money(i.price * i.quantity)}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      <div className="summary-line">
        <strong>Order total</strong>
        <strong>{money(order.total)}</strong>
      </div>
    </div>
  )
}
